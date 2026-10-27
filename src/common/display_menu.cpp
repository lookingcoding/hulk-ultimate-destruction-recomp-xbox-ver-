#include "common/display_menu.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <vector>
#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace hulk {
namespace {

// default.xbe (Europe)
constexpr uint32_t kGuestNew = 0x0008D730;             // operator new, cdecl
constexpr uint32_t kRegisterIntCallback = 0x000FC420;  // cdecl (name, functor): keeps the name's hash and the functor
constexpr uint32_t kIntFunctorVtable = 0x00409B40;     // functor: vtable, owner, method (thiscall owner, int value)
constexpr uint32_t kSharedHandler = 0x000BBEC0;        // AudioQualityChanged, PS2 and GameCube only: hooks.cpp
constexpr uint32_t kRegisterCallback = 0x000FC400;     // cdecl (name, functor): a function the scripts call
constexpr uint32_t kFunctorVtable = 0x00409B30;        // functor: method (thiscall owner, argc, argv) returning int
constexpr uint32_t kSyncHandler = 0x000B46B0;          // Print, which no Xbox script calls: hooks.cpp
// a screen's selectors start where the game's code sets them (the camera options at 0x000B5C50 do so):
constexpr uint32_t kFindWidget = 0x000FC480;           // cdecl (name): the widget of the current screen
constexpr uint32_t kDynamicCast = 0x00340764;          // __RTDynamicCast (object, 0, from, to, 0)
constexpr uint32_t kWidgetType = 0x0049FCE4, kTextSelectorType = 0x0049FCFC;
constexpr uint32_t kSetSelection = 0x64;               // TextSelector vtable offset, thiscall (index)
// the selectors' functors carry these as owners: the guest's null page, never an object; kSettings is the sync
constexpr uint32_t kOwnerBase = 0x100;
constexpr const char* kSyncName = "PCDisplaySync";

constexpr uint32_t kRcfAlign = 0x800;
constexpr auto kSettleTime = std::chrono::milliseconds(400);

enum Setting { kMode, kResolution, kFrameRate, kVSync, kScale, kSettings };

struct Entry {
    const char* key;  // string table key of its text
    uint32_t a, b;    // the setting's value: mode, width and height, fps cap, vsync, percent
};

struct Option {
    const char* named_value;  // the FE's named value, its callback is this + "Changed"
    const char* selector;     // the selector's name in the screen
    const char* label;        // string table key
    std::vector<Entry> entries;
};

// a string table line in each language: en and uk, fr, es, it
struct Text {
    const char* key;
    const char* text[4];
};

const Text kTexts[] = {
    {"TBS_PC_DISPLAY_OPTIONS", {"Display Options", "Options d'affichage", "Opciones de pantalla", "Opzioni schermo"}},
    {"TBS_PC_DISPLAY_MODE", {"Display Mode", "Mode d'affichage", "Modo de pantalla", "Modalità schermo"}},
    {"TBS_PC_FULLSCREEN", {"Fullscreen", "Plein écran", "Pantalla completa", "Schermo intero"}},
    {"TBS_PC_BORDERLESS", {"Borderless", "Fenêtre sans bordure", "Ventana sin bordes", "Finestra senza bordi"}},
    {"TBS_PC_WINDOWED", {"Windowed", "Fenêtré", "Ventana", "Finestra"}},
    {"TBS_PC_RESOLUTION", {"Resolution", "Résolution", "Resolución", "Risoluzione"}},
    {"TBS_PC_RES_DESKTOP", {"Desktop", "Bureau", "Escritorio", "Desktop"}},
    {"TBS_PC_RES_1280X720", {"1280 x 720", "1280 x 720", "1280 x 720", "1280 x 720"}},
    {"TBS_PC_RES_1600X900", {"1600 x 900", "1600 x 900", "1600 x 900", "1600 x 900"}},
    {"TBS_PC_RES_1920X1080", {"1920 x 1080", "1920 x 1080", "1920 x 1080", "1920 x 1080"}},
    {"TBS_PC_RES_2560X1440", {"2560 x 1440", "2560 x 1440", "2560 x 1440", "2560 x 1440"}},
    {"TBS_PC_RES_3840X2160", {"3840 x 2160", "3840 x 2160", "3840 x 2160", "3840 x 2160"}},
    {"TBS_PC_FRAME_RATE", {"Frame Rate Limit", "Limite d'images", "Límite de fotogramas", "Limite fotogrammi"}},
    {"TBS_PC_FPS_30", {"30", "30", "30", "30"}},
    {"TBS_PC_FPS_60", {"60", "60", "60", "60"}},
    {"TBS_PC_FPS_120", {"120", "120", "120", "120"}},
    {"TBS_PC_FPS_144", {"144", "144", "144", "144"}},
    {"TBS_PC_FPS_REFRESH", {"Refresh Rate", "Fréquence de l'écran", "Frecuencia de pantalla", "Frequenza schermo"}},
    {"TBS_PC_FPS_UNLIMITED", {"Unlimited", "Illimitée", "Ilimitado", "Illimitato"}},
    {"TBS_PC_VSYNC", {"V-Sync", "Synchro verticale", "Sincronización vertical", "Sincronia verticale"}},
    {"TBS_PC_RENDER_SCALE", {"Render Scale", "Échelle de rendu", "Escala de renderizado", "Scala di rendering"}},
    {"TBS_PC_SCALE_50", {"50%", "50 %", "50%", "50%"}},
    {"TBS_PC_SCALE_75", {"75%", "75 %", "75%", "75%"}},
    {"TBS_PC_SCALE_100", {"100%", "100 %", "100%", "100%"}},
    {"TBS_PC_SCALE_150", {"150%", "150 %", "150%", "150%"}},
    {"TBS_PC_SCALE_200", {"200%", "200 %", "200%", "200%"}},
    {"TBS_PC_CUSTOM", {"Custom", "Personnalisé", "Personalizado", "Personalizzato"}},
};

// the string tables and the column of kTexts each takes
const std::pair<const char*, int> kLanguages[] = {
    {"frontend\\lang\\str_en.txt", 0}, {"frontend\\lang\\str_uk.txt", 0}, {"frontend\\lang\\str_fr.txt", 1},
    {"frontend\\lang\\str_es.txt", 2}, {"frontend\\lang\\str_it.txt", 3},
};

bool Matches(Setting s, const Entry& e, const app::Settings& st) {
    switch (s) {
        case kMode: return uint32_t(st.display_mode) == e.a;
        case kResolution: return st.width == e.a && st.height == e.b;
        case kFrameRate: return st.fps_cap == e.a;
        case kVSync: return uint32_t(st.vsync) == e.a;
        case kScale: return st.render_percent == e.a;
        default: return false;
    }
}

void Assign(Setting s, const Entry& e, app::Settings& st) {
    switch (s) {
        case kMode: st.display_mode = app::DisplayMode(e.a); break;
        case kResolution: st.width = e.a, st.height = e.b; break;
        case kFrameRate: st.fps_cap = e.a; break;
        case kVSync: st.vsync = e.a != 0; break;
        case kScale: st.render_percent = e.a; break;
        default: break;
    }
}

// the selectors' choices; a value the settings file holds outside them is kept as Custom
std::vector<Option> MakeOptions(const app::Settings& st) {
    std::vector<Option> o = {
        {"PCDisplayMode", "pc_display_mode_selector", "TBS_PC_DISPLAY_MODE",
         {{"TBS_PC_FULLSCREEN", uint32_t(app::DisplayMode::Fullscreen), 0},
          {"TBS_PC_BORDERLESS", uint32_t(app::DisplayMode::Borderless), 0},
          {"TBS_PC_WINDOWED", uint32_t(app::DisplayMode::Windowed), 0}}},
        {"PCResolution", "pc_resolution_selector", "TBS_PC_RESOLUTION",
         {{"TBS_PC_RES_DESKTOP", 0, 0},
          {"TBS_PC_RES_1280X720", 1280, 720},
          {"TBS_PC_RES_1600X900", 1600, 900},
          {"TBS_PC_RES_1920X1080", 1920, 1080},
          {"TBS_PC_RES_2560X1440", 2560, 1440},
          {"TBS_PC_RES_3840X2160", 3840, 2160}}},
        {"PCFrameRate", "pc_frame_rate_selector", "TBS_PC_FRAME_RATE",
         {{"TBS_PC_FPS_30", 30, 0},
          {"TBS_PC_FPS_60", 60, 0},
          {"TBS_PC_FPS_120", 120, 0},
          {"TBS_PC_FPS_144", 144, 0},
          {"TBS_PC_FPS_REFRESH", app::kFpsCapRefresh, 0},
          {"TBS_PC_FPS_UNLIMITED", 0, 0}}},
        {"PCVSync", "pc_vsync_selector", "TBS_PC_VSYNC",
         {{"TBS_MENU_ITEM_CHOICE_ON", 1, 0}, {"TBS_MENU_ITEM_CHOICE_OFF", 0, 0}}},
        {"PCRenderScale", "pc_render_scale_selector", "TBS_PC_RENDER_SCALE",
         {{"TBS_PC_SCALE_50", 50, 0},
          {"TBS_PC_SCALE_75", 75, 0},
          {"TBS_PC_SCALE_100", 100, 0},
          {"TBS_PC_SCALE_150", 150, 0},
          {"TBS_PC_SCALE_200", 200, 0}}},
    };
    for (int s = 0; s < kSettings; s++) {
        auto& entries = o[s].entries;
        if (std::none_of(entries.begin(), entries.end(), [&](const Entry& e) { return Matches(Setting(s), e, st); })) {
            Entry custom{"TBS_PC_CUSTOM", 0, 0};
            switch (s) {
                case kMode: custom.a = uint32_t(st.display_mode); break;
                case kResolution: custom.a = st.width, custom.b = st.height; break;
                case kFrameRate: custom.a = st.fps_cap; break;
                case kVSync: custom.a = st.vsync; break;
                case kScale: custom.a = st.render_percent; break;
            }
            entries.push_back(custom);
        }
    }
    return o;
}

std::mutex g_lock;
std::vector<Option> g_options;  // made once when misc.rcf is first opened, before the frontend loads
std::atomic<bool> g_ready{false};   // g_options is made and stays as it is
std::atomic<bool> g_served{false};  // the game reads the edited script
std::atomic<int64_t> g_changed_at{0};  // steady clock ticks of the last unapplied change, 0 for none

// the selection a selector starts at: the current setting's (one digit, the script's bytes keep their length)
char Selection(Setting s) {
    const auto& st = app::Get();
    const auto& entries = g_options[s].entries;
    for (size_t i = 0; i < entries.size(); i++)
        if (Matches(s, entries[i], st)) return char('0' + i);
    return '0';
}

// where a frontend script puts its rows: the main menu (fe_real.txt) or the pause menu (fe_run.txt)
struct Layout {
    const char* file;
    const char* item;   // the Options screen row after Camera Options
    const char* title;  // the screen title label
    const char* row;    // the screen rows, numbered from 1
    bool main;          // the main menu formats rows with its menu_item_N macros, the pause menu with select bars
};
const Layout kLayouts[] = {
    {"frontend\\fe_real.txt", "FEmain_left_item5", "FEsubMenu_title", "FEsubMenu_text", true},  // FESlotName in feconfig.txt
    {"frontend\\fe_run.txt", "FEpause_text4", "FEpause_title", "FEpause_text", false},
};

// a script, edited: a row after Camera Options in its Options screen, the selectors' named values and the
// Display Options screen before its camera options screen
struct Script {
    std::string text;
    size_t selection[kSettings];  // offsets of the named values' digits
};

std::optional<Script> EditScript(const std::string& in, const Layout& l) {
    const std::string nl = in.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string options_screen = "AddScreen \"options\"";
    const std::string camera_row = "OnSelect gotoscreen \"camera_options\"" + nl;
    const std::string row_end = "    }" + nl;
    const std::string screen_at = "AddScreen \"camera_options\"";

    size_t options = in.find(options_screen);
    size_t camera = options == std::string::npos ? options : in.find(camera_row, options);
    size_t row = camera == std::string::npos ? camera : in.find(row_end, camera);
    size_t screen = in.find(screen_at);
    if (row == std::string::npos || screen == std::string::npos || row > screen) {
        LOGW("display menu: %s is not the script it expects, its Options stays as it is", l.file);
        return std::nullopt;
    }
    row += row_end.size();
    auto bars = [&](const std::string& n) {
        if (l.main) return "        $MACRO_INSERT menu_item_" + n + nl;
        return nl + "        SelectedDrawable \"FE_selectBar\"" + nl + "        NotSelectedDrawable \"FE_backgroundBar\"" + nl + nl +
               "        TransitionAnimEnter \"slide_down" + n + "\"" + nl + "        TransitionAnimExit  \"slide_down" + n + "\"" + nl;
    };

    std::string item;
    item += nl;
    item += std::string("    AddTextButton \"") + l.item + "\"" + nl;
    item += "    {" + nl;
    item += "        text \"TBS_PC_DISPLAY_OPTIONS\" 1" + nl;
    item += "        OnSelect gotoscreen \"display_options\"" + nl;
    item += "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\"  \"1\"" + nl;
    if (l.main) item += bars("5") + "        halign left" + nl;
    else item += nl + "        $MACRO_INSERT FormatText" + nl + bars("4");
    item += "    }" + nl;

    std::string sub;
    sub += "# PC display options (Hulk Recompiled): the selectors call back the host" + nl;
    std::vector<size_t> digits;  // within sub
    for (int s = 0; s < kSettings; s++) {
        sub += std::string("AddNamedValueInt \"") + g_options[s].named_value + "\" ";
        digits.push_back(sub.size());
        sub += Selection(Setting(s));
        sub += std::string(" \"") + g_options[s].named_value + "Changed\"" + nl;
    }
    sub += nl + "AddScreen \"display_options\"" + nl;
    sub += "{" + nl;
    sub += "    $MACRO_INSERT typicalScreenSetup" + nl + nl;
    sub += std::string("    AddLabel \"") + l.title + "\"" + nl;
    sub += "    {" + nl;
    sub += "        text \"TBS_PC_DISPLAY_OPTIONS\" 1" + nl;
    sub += "        $MACRO_INSERT FormatTitleText" + nl + nl;
    sub += "        TransitionAnimEnter \"fadein\"" + nl;
    sub += "        TransitionAnimExit  \"fadeout\"" + nl;
    sub += "    }" + nl;
    for (int s = 0; s < kSettings; s++) {
        const Option& o = g_options[s];
        std::string row_n = std::to_string(s + 1);
        sub += nl + "    AddTextSelector \"" + l.row + row_n + "\"" + nl;
        sub += "    {" + nl;
        sub += std::string("        text \"") + o.label + "\" 1" + nl + nl;
        sub += std::string("        name \"") + o.selector + "\"" + nl;
        sub += "        TextSelection \"\"";
        for (const Entry& e : o.entries) sub += std::string(" ") + e.key;
        sub += nl;
        sub += std::string("        NamedValue \"") + o.named_value + "\"" + nl + nl;
        sub += "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\"  \"2\"" + nl;
        sub += std::string("        OnHighlight function \"") + kSyncName + "\"" + nl;
        sub += bars(row_n);
        sub += "    }" + nl;
    }
    sub += nl + "    $MACRO_INSERT SetupControlButtonLabel" + nl;
    sub += "}" + nl + nl;

    Script out{};
    out.text = in.substr(0, row) + item + in.substr(row, screen - row) + sub + in.substr(screen);
    for (int s = 0; s < kSettings; s++) out.selection[s] = screen + item.size() + digits[s];
    return out;
}

void AppendUtf16(std::vector<uint8_t>& out, const char* utf8) {
    for (const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8); *p;) {
        uint32_t c = *p++;
        if (c >= 0xE0) {
            c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0xC0) {
            c = (c & 0x1F) << 6 | (p[0] & 0x3F);
            p += 1;
        }
        out.push_back(uint8_t(c));
        out.push_back(uint8_t(c >> 8));
    }
}

// a string table (UTF-16 lines of key, tab, text) with the menu's lines after its own
std::optional<std::vector<uint8_t>> EditStrings(const std::vector<uint8_t>& in, int column, const char* name) {
    if (in.size() < 4 || in[0] != 0xFF || in[1] != 0xFE) {
        LOGW("display menu: %s is not a UTF-16 string table", name);
        return std::nullopt;
    }
    std::vector<uint8_t> out = in;
    bool ends_with_newline = out[out.size() - 2] == '\n' && out[out.size() - 1] == 0;
    if (!ends_with_newline) AppendUtf16(out, "\r\n");
    for (const Text& t : kTexts) {
        AppendUtf16(out, t.key);
        AppendUtf16(out, "\t");
        AppendUtf16(out, t.text[column]);
        AppendUtf16(out, "\r\n");
    }
    return out;
}

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= b.size()) std::memcpy(&v, b.data() + at, 4);
    return v;
}

bool EqualNoCase(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
    return true;
}

// misc.rcf as the game reads it: the edited files past its end, the directory pointing at them
struct RcfView {
    uint64_t dir_at = 0;
    std::vector<uint8_t> dir;  // the directory, edited
    struct Added {
        uint64_t at;
        std::vector<uint8_t> bytes;
        bool script;                  // the selections follow the settings, the rest stays
        size_t selection[kSettings];  // a script's named values' digits
    };
    std::vector<Added> added;

    void Read(uint64_t at, uint8_t* dst, size_t n) const {
        auto overlay = [&](uint64_t from, const uint8_t* src, size_t len) {
            uint64_t lo = std::max(at, from), hi = std::min<uint64_t>(at + n, from + len);
            if (lo < hi) std::memcpy(dst + (lo - at), src + (lo - from), size_t(hi - lo));
        };
        overlay(dir_at, dir.data(), dir.size());
        for (const Added& a : added) {
            if (!a.script) {
                overlay(a.at, a.bytes.data(), a.bytes.size());
                continue;
            }
            char digits[kSettings];
            for (int s = 0; s < kSettings; s++) digits[s] = Selection(Setting(s));
            overlay(a.at, a.bytes.data(), a.bytes.size());
            for (int s = 0; s < kSettings; s++)
                overlay(a.at + a.selection[s], reinterpret_cast<const uint8_t*>(&digits[s]), 1);
        }
    }
};

std::shared_ptr<RcfView> MakeRcfView(uint64_t size, const std::function<size_t(uint64_t, void*, size_t)>& read) {
    auto bytes = [&](uint64_t at, size_t n) {
        std::vector<uint8_t> b(n);
        b.resize(read(at, b.data(), n));
        return b;
    };
    // header: "ATG CORE CEMENT LIBRARY", then at 0x24 the directory's offset and size, the names' offset and size,
    // and at 0x38 the file count; directory entries are name hash, offset, size, sorted by hash; the names follow
    // the files' order in the archive
    std::vector<uint8_t> head = bytes(0, 0x40);
    if (head.size() < 0x40 || std::memcmp(head.data(), "ATG CORE CEMENT LIBRARY", 23) != 0) return nullptr;
    uint32_t dir_at = U32(head, 0x24), names_at = U32(head, 0x2C), names_size = U32(head, 0x30), count = U32(head, 0x38);
    if (!count || count > 0x10000 || dir_at + uint64_t(count) * 12 > size || names_at + uint64_t(names_size) > size)
        return nullptr;
    std::vector<uint8_t> dir = bytes(dir_at, count * 12), names = bytes(names_at, names_size);
    if (dir.size() != count * 12 || names.size() != names_size) return nullptr;

    std::vector<uint32_t> order(count);
    for (uint32_t i = 0; i < count; i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
        uint32_t ox = U32(dir, x * 12 + 4), oy = U32(dir, y * 12 + 4);
        return ox != oy ? ox < oy : U32(dir, x * 12 + 8) < U32(dir, y * 12 + 8);
    });
    // a name: time, alignment, 0, length with its terminator, the name, 3 bytes of padding
    std::vector<std::pair<std::string, uint32_t>> files;  // name, directory entry
    size_t p = 8;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len = U32(names, p + 12);
        p += 16;
        if (!len || p + len > names.size()) return nullptr;
        files.emplace_back(std::string(reinterpret_cast<const char*>(names.data() + p), len - 1), order[i]);
        p += len + 3;
    }
    auto find = [&](const char* name) -> std::optional<uint32_t> {
        for (auto& [n, e] : files)
            if (EqualNoCase(n, name)) return e;
        return std::nullopt;
    };
    auto contents = [&](uint32_t e) { return bytes(U32(dir, e * 12 + 4), U32(dir, e * 12 + 8)); };

    auto view = std::make_shared<RcfView>();
    view->dir_at = dir_at;
    uint64_t end = (size + kRcfAlign - 1) & ~uint64_t(kRcfAlign - 1);
    auto add = [&](uint32_t e, std::vector<uint8_t> b, const Script* script) {
        uint32_t at = uint32_t(end), n = uint32_t(b.size());
        std::memcpy(dir.data() + e * 12 + 4, &at, 4);
        std::memcpy(dir.data() + e * 12 + 8, &n, 4);
        RcfView::Added a{end, std::move(b), script != nullptr, {}};
        if (script) std::copy(std::begin(script->selection), std::end(script->selection), a.selection);
        view->added.push_back(std::move(a));
        end = (end + n + kRcfAlign - 1) & ~uint64_t(kRcfAlign - 1);
    };

    std::vector<std::pair<uint32_t, Script>> scripts;  // directory entry, edited
    for (const Layout& l : kLayouts) {
        auto e = find(l.file);
        if (!e) continue;
        std::vector<uint8_t> raw = contents(*e);
        if (auto script = EditScript(std::string(raw.begin(), raw.end()), l)) scripts.emplace_back(*e, std::move(*script));
    }
    if (scripts.empty()) return nullptr;
    for (auto& [name, column] : kLanguages) {
        auto e = find(name);
        if (!e) continue;  // a release without that language
        auto edited = EditStrings(contents(*e), column, name);
        if (!edited) return nullptr;
        add(*e, std::move(*edited), nullptr);
    }
    for (auto& [e, script] : scripts) add(e, std::vector<uint8_t>(script.text.begin(), script.text.end()), &script);
    view->dir = std::move(dir);
    return view;
}

}  // namespace

std::shared_ptr<app::FileView> GameFileView(const std::string& path, uint64_t size,
                                            const std::function<size_t(uint64_t, void*, size_t)>& read) {
    const std::string suffix = "\\misc.rcf";
    if (path.size() < suffix.size() || !EqualNoCase(path.substr(path.size() - suffix.size()), suffix.c_str()))
        return nullptr;
    {
        std::lock_guard lock(g_lock);
        if (!g_ready.load()) {
            g_options = MakeOptions(app::Get());
            g_ready.store(true);
        }
    }
    std::shared_ptr<RcfView> rcf = MakeRcfView(size, read);
    if (!rcf) {
        LOGW("display menu: %s could not be read as a cement library, Options stays as it is", path.c_str());
        return nullptr;
    }
    g_served.store(true);
    auto view = std::make_shared<app::FileView>();
    const auto& last = rcf->added.back();
    view->size = last.at + last.bytes.size();
    view->read = [rcf](uint64_t at, uint8_t* dst, size_t n) { rcf->Read(at, dst, n); };
    LOGI("display menu: %s serves Options > Display Options (%zu files edited)", path.c_str(), rcf->added.size());
    return view;
}

void RegisterDisplayCallbacks(X86Context& ctx) {
    if (!g_served.load()) return;  // the game reads its own script, which names none of them
    const uint32_t esp = ctx.esp;
    for (uint32_t s = 0; s <= kSettings; s++) {
        // the name only lives through the call, below the guest's stack
        bool sync = s == kSettings;
        std::string name = sync ? kSyncName : std::string(g_options[s].named_value) + "Changed";
        ctx.esp = (esp - 64) & ~3u;
        std::memcpy(kernel::HostPtr(ctx.esp), name.c_str(), name.size() + 1);
        uint32_t functor = kernel::CallGuest(ctx, kGuestNew, {12});
        if (!functor) break;
        kernel::W32(functor, sync ? kFunctorVtable : kIntFunctorVtable);
        kernel::W32(functor + 4, kOwnerBase + s);
        kernel::W32(functor + 8, sync ? kSyncHandler : kSharedHandler);
        kernel::CallGuest(ctx, sync ? kRegisterCallback : kRegisterIntCallback, {ctx.esp, functor});
    }
    ctx.esp = esp;
}

bool DisplaySync(X86Context& ctx, uint32_t owner) {
    if (owner != kOwnerBase + kSettings) return false;
    if (!g_ready.load()) return true;
    const uint32_t esp = ctx.esp, ecx = ctx.ecx;
    for (int s = 0; s < kSettings; s++) {
        const char* name = g_options[s].selector;
        ctx.esp = (esp - 64) & ~3u;
        std::memcpy(kernel::HostPtr(ctx.esp), name, std::strlen(name) + 1);
        uint32_t widget = kernel::CallGuest(ctx, kFindWidget, {ctx.esp});
        uint32_t selector = widget ? kernel::CallGuest(ctx, kDynamicCast, {widget, 0, kWidgetType, kTextSelectorType, 0}) : 0;
        if (!selector) continue;
        ctx.ecx = selector;
        kernel::CallGuest(ctx, kernel::R32(kernel::R32(selector) + kSetSelection), {uint32_t(Selection(Setting(s)) - '0')});
    }
    ctx.esp = esp;
    ctx.ecx = ecx;
    return true;
}

bool DisplayCallback(uint32_t owner, uint32_t value) {
    if (owner < kOwnerBase || owner >= kOwnerBase + kSettings) return false;
    Setting s = Setting(owner - kOwnerBase);
    if (!g_ready.load() || value >= g_options[s].entries.size()) return true;
    const Entry& e = g_options[s].entries[value];
    app::Settings& st = app::Get();
    if (Matches(s, e, st)) return true;
    Assign(s, e, st);
    LOGI("display menu: %s -> %s", g_options[s].named_value, e.key);
    g_changed_at.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    return true;
}

void TickDisplaySettings() {
    int64_t at = g_changed_at.load(std::memory_order_relaxed);
    if (!at) return;
    auto since = std::chrono::steady_clock::now() - std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(at));
    if (since < kSettleTime || !g_changed_at.compare_exchange_strong(at, 0, std::memory_order_relaxed)) return;
    app::ApplyDisplay();
    app::Save();
}

}  // namespace hulk
