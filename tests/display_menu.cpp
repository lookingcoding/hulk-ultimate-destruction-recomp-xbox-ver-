#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "app/app.h"
#include "common/display_menu.h"
#include "kernel/kernel.h"

// Serve the real misc.rcf through the display menu's view and read it back as the game's cement library reader does.
namespace app {
static Settings settings;
static int applied = 0, saved = 0;
Settings& Get() { return settings; }
void ApplyDisplay() { applied++; }
void Save() { saved++; }
}  // namespace app
namespace logx {
void Write(Level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}
}  // namespace logx
namespace kernel {
uint8_t* g_base;
static std::vector<std::pair<std::string, std::vector<uint32_t>>> registered;  // name, functor
static uint32_t next_alloc = 0x10000;
// the screen's widgets: name -> address, each a TextSelector whose vtable's SetSelection is kSetSelection
static std::vector<std::string> widgets;
static std::vector<std::pair<uint32_t, uint32_t>> selections;  // widget, index
constexpr uint32_t kWidgets = 0x40000, kVtable = 0x50000, kSetSelection = 0x777000;
uint32_t CallGuest(X86Context& ctx, uint32_t fn, const uint32_t* args, size_t count) {
    if (fn == 0x0008D730 && count == 1) {
        uint32_t p = next_alloc;
        next_alloc += (args[0] + 15) & ~15u;
        return p;
    }
    if ((fn == 0x000FC420 || fn == 0x000FC400) && count == 2) {
        registered.push_back({reinterpret_cast<const char*>(g_base + args[0]), {R32(args[1]), R32(args[1] + 4), R32(args[1] + 8)}});
        return 0;
    }
    if (fn == 0x000FC480 && count == 1) {
        widgets.push_back(reinterpret_cast<const char*>(g_base + args[0]));
        uint32_t w = kWidgets + uint32_t(widgets.size()) * 16;
        W32(w, kVtable);
        W32(kVtable + 0x64, kSetSelection);
        return w;
    }
    if (fn == 0x00340764 && count == 5 && args[2] == 0x0049FCE4 && args[3] == 0x0049FCFC) return args[0];
    if (fn == kSetSelection && count == 1) {
        selections.push_back({ctx.ecx, args[0]});
        return 0;
    }
    std::printf("unexpected guest call %08X\n", fn);
    return 0;
}
}  // namespace kernel

static int failures = 0;
static bool Check(bool value, const char* message) {
    if (!value) {
        std::printf("FAIL: %s\n", message);
        failures++;
    }
    return value;
}

static uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

static std::string Utf16(const std::vector<uint8_t>& b) {
    std::string s;
    for (size_t i = 2; i + 1 < b.size(); i += 2) s += b[i + 1] ? '?' : char(b[i]);
    return s;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "assets/misc.rcf";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::printf("SKIP: %s is not here\n", path);
        return 0;
    }
    std::vector<uint8_t> own((std::istreambuf_iterator<char>(in)), {});
    auto read = [&](uint64_t at, void* dst, size_t n) -> size_t {
        if (at >= own.size()) return 0;
        n = std::min<size_t>(n, own.size() - at);
        std::memcpy(dst, own.data() + at, n);
        return n;
    };

    app::settings.display_mode = app::DisplayMode::Borderless;
    app::settings.width = 1920, app::settings.height = 1080;
    app::settings.fps_cap = 90;  // not a choice: Custom
    app::settings.vsync = true;
    app::settings.render_percent = 100;

    Check(!hulk::GameFileView("D:\\art.rcf", own.size(), read), "only misc.rcf gets a view");
    auto view = hulk::GameFileView("D:\\misc.rcf", own.size(), read);
    if (!Check(view != nullptr, "misc.rcf gets a view")) return 1;
    Check(view->size > own.size(), "the edited files lengthen the archive");

    // the archive as the game reads it: its own bytes, zeros past the end, then the view
    auto served = [&] {
        std::vector<uint8_t> b(view->size, 0);
        read(0, b.data(), own.size());
        view->read(0, b.data(), b.size());
        return b;
    };
    std::vector<uint8_t> rcf = served();
    Check(std::memcmp(rcf.data(), own.data(), U32(own, 0x24)) == 0, "the header stays");

    // the directory's entries for the original files, matched by hash
    uint32_t dir = U32(own, 0x24), count = U32(own, 0x38);
    std::vector<std::string> scripts, tables;
    size_t moved = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = U32(rcf, dir + i * 12 + 4), size = U32(rcf, dir + i * 12 + 8);
        Check(U32(rcf, dir + i * 12) == U32(own, dir + i * 12), "hashes stay");
        if (off == U32(own, dir + i * 12 + 4)) continue;
        moved++;
        Check(off >= own.size() && off % 0x800 == 0 && off + size <= rcf.size(), "moved files sit aligned past the end");
        std::vector<uint8_t> file(rcf.begin() + off, rcf.begin() + off + size);
        if (file.size() > 2 && file[0] == 0xFF && file[1] == 0xFE) tables.push_back(Utf16(file));
        else scripts.emplace_back(file.begin(), file.end());
    }
    // the European disc has five string tables, the North American one English only
    Check(scripts.size() == 2 && (tables.size() == 5 || tables.size() == 1) && moved == scripts.size() + tables.size(),
          "fe_real.txt, fe_run.txt and each string table moved");
    for (const std::string& t : tables) {
        Check(t.find("TBS_PC_DISPLAY_OPTIONS\t") != std::string::npos, "string tables have the menu's keys");
        Check(t.find("TBS_MENU_ITEM_OM_CAMERA_OPTIONS\t") != std::string::npos, "string tables keep their own");
    }
    // without a controller the prompts are the keyboard's; the map legend's icons stay
    Check(tables[0].find("press [Q] to pick it up") != std::string::npos && tables[0].find("\\bus") == std::string::npos &&
              tables[0].find("\\bmh Hulk") != std::string::npos,
          "keyboard prompts in place of the controller's");
    Check(tables[0].find("Press [Space] to start the exercise") != std::string::npos, "the select button's prompt is the A button's key");
    Check(tables[0].find("TBS_MENU_ITEM_PRESS_START2_XBOX\tPress Enter") != std::string::npos &&
              tables[0].find("TBS_MENU_ITEM_PRESS_START3\tPress any key") != std::string::npos &&
              tables[0].find("TBS_MENU_ITEM_PRESS_START\t[Space] Start") != std::string::npos,
          "the title screen asks for Enter and any key, not START or a controller glyph");
    std::string main_menu, pause_menu;
    for (const std::string& s : scripts) (s.find("AddScreen \"main\"") != std::string::npos ? main_menu : pause_menu) = s;
    for (const std::string* script : {&main_menu, &pause_menu}) {
        Check(script->find("AddScreen \"display_options\"") != std::string::npos, "each script has the Display Options screen");
        size_t options = script->find("AddScreen \"options\"");
        size_t item = script->find("        text \"TBS_PC_DISPLAY_OPTIONS\" 1\r\n        OnSelect gotoscreen \"display_options\"");
        size_t next_screen = script->find("AddScreen", options + 1);
        Check(options < item && item < next_screen, "its Options screen has a row opening it");
        Check(script->find("AddNamedValueInt \"PCDisplayMode\" 1 \"PCDisplayModeChanged\"") != std::string::npos, "borderless selected");
        Check(script->find("AddNamedValueInt \"PCResolution\" 3 \"PCResolutionChanged\"") != std::string::npos, "1920 x 1080 selected");
        Check(script->find("AddNamedValueInt \"PCFrameRate\" 6 \"PCFrameRateChanged\"") != std::string::npos, "custom fps selected");
        Check(script->find("TBS_PC_FPS_UNLIMITED TBS_PC_CUSTOM\r\n") != std::string::npos, "custom fps is a choice");
        Check(script->find("AddNamedValueInt \"PCVSync\" 0 \"PCVSyncChanged\"") != std::string::npos, "vsync on selected");
        Check(script->find("AddNamedValueInt \"PCRenderScale\" 2 \"PCRenderScaleChanged\"") != std::string::npos, "100% selected");
        Check(script->find("        OnHighlight function \"PCDisplaySync\"\r\n") != std::string::npos, "the selectors sync on highlight");
        Check(script->find("TBS_MENU_ITEM_NO TBS_MENU_ITEM_YES\r\n        NamedValue \"CameraInvertX\"") != std::string::npos &&
                  script->find("TBS_MENU_ITEM_NO TBS_MENU_ITEM_YES\r\n        NamedValue \"CameraInvertY\"") != std::string::npos,
              "Invert X and Y name the camera that looks up with the stick No");
    }
    Check(main_menu.find("AddTextButton \"FEmain_left_item5\"\r\n    {\r\n        text \"TBS_PC_DISPLAY_OPTIONS\"") != std::string::npos &&
              main_menu.find("AddTextSelector \"FEsubMenu_text5\"") != std::string::npos,
          "the main menu's slots");
    Check(main_menu.find("ifplatform PS2 GAMECUBE\r\n{\r\n    AddTextSelector \"FEmain_left_item5\"") != std::string::npos,
          "the PS2 and GameCube widescreen row stays");
    Check(pause_menu.find("AddTextButton \"FEpause_text4\"\r\n    {\r\n        text \"TBS_PC_DISPLAY_OPTIONS\"") != std::string::npos &&
              pause_menu.find("AddTextSelector \"FEpause_text5\"") != std::string::npos &&
              pause_menu.find("        TransitionAnimEnter \"slide_down5\"") != std::string::npos,
          "the pause menu's slots and select bars");
    if (argc > 2) std::ofstream(argv[2], std::ios::binary).write(main_menu.data(), std::streamsize(main_menu.size()));
    if (argc > 3) std::ofstream(argv[3], std::ios::binary).write(pause_menu.data(), std::streamsize(pause_menu.size()));

    // the selectors' callbacks: registered after the game's own, then called with the selection
    std::vector<uint8_t> memory(0x100000);
    kernel::g_base = memory.data();
    X86Context ctx{};
    ctx.esp = 0x80000;
    hulk::RegisterDisplayCallbacks(ctx);
    Check(ctx.esp == 0x80000, "the guest's stack is as it was");
    Check(kernel::registered.size() == 6, "five selector callbacks and the sync");
    if (kernel::registered.size() == 6) {
        Check(kernel::registered[1].first == "PCResolutionChanged", "named as the script names them");
        Check(kernel::registered[1].second == std::vector<uint32_t>({0x409B40, 0x101, 0xBBEC0}), "functor through the shared handler");
        Check(kernel::registered[5].first == "PCDisplaySync", "the sync");
        Check(kernel::registered[5].second == std::vector<uint32_t>({0x409B30, 0x105, 0xB46B0}), "sync through Print");
    }
    Check(!hulk::DisplaySync(ctx, 0x00123456), "a real Print owner goes to the game");
    ctx.ecx = 0xABCD;
    Check(hulk::DisplaySync(ctx, 0x105), "the sync");
    Check(ctx.esp == 0x80000 && ctx.ecx == 0xABCD, "the sync leaves the registers as they were");
    Check(kernel::widgets == std::vector<std::string>({"pc_display_mode_selector", "pc_resolution_selector",
                                                        "pc_frame_rate_selector", "pc_vsync_selector", "pc_render_scale_selector"}),
          "each selector found by its name");
    Check(kernel::selections == std::vector<std::pair<uint32_t, uint32_t>>({{0x40010, 1}, {0x40020, 3}, {0x40030, 6}, {0x40040, 0}, {0x40050, 2}}),
          "each set to the current setting");
    Check(!hulk::DisplayCallback(0x00123456, 1), "a real AudioQuality owner goes to the game");
    Check(hulk::DisplayCallback(0x101, 5), "resolution selector");
    Check(app::settings.width == 3840 && app::settings.height == 2160, "3840 x 2160 chosen");
    Check(hulk::DisplayCallback(0x100, 2) && app::settings.display_mode == app::DisplayMode::Windowed, "windowed chosen");
    Check(hulk::DisplayCallback(0x101, 99) && app::settings.width == 3840, "a selection past the choices is ignored");
    hulk::TickDisplaySettings();
    Check(app::applied == 0, "changes wait to settle");
    std::vector<uint8_t> after = served();
    std::string again(after.begin(), after.end());
    Check(again.find("AddNamedValueInt \"PCResolution\" 5 \"PCResolutionChanged\"") != std::string::npos &&
              again.find("AddNamedValueInt \"PCDisplayMode\" 2 \"PCDisplayModeChanged\"") != std::string::npos,
          "the script read again starts at the settings as they are");
    Check(again.size() == rcf.size(), "the archive keeps its size");


    std::printf(failures ? "%d failed\n" : "display menu: all passed\n", failures);
    return failures ? 1 : 0;
}
