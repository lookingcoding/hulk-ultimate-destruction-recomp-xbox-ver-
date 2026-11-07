#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>
#include "app/app.h"
#include "common/display_menu.h"
#include "common/language.h"
#include "common/view.h"
#include "common/rcf.h"
#include "common/mods.h"
#include "kernel/kernel.h"

// Serve the real misc.rcf through the display menu's view and read it back as the game's cement library reader does.
namespace app {
static Settings settings;
static int applied = 0, saved = 0;
static std::filesystem::path test_exe_dir;
std::filesystem::path ExeDir() { return test_exe_dir; }
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

static hulk::ReadFn Reader(const std::vector<uint8_t>& bytes) {
    return [&bytes](uint64_t at, void* dst, size_t n) -> size_t {
        if (at >= bytes.size()) return 0;
        n = std::min<size_t>(n, bytes.size() - at);
        std::memcpy(dst, bytes.data() + at, n);
        return n;
    };
}
static std::string Payload(const std::vector<uint8_t>& bytes, const hulk::Rcf& rcf, uint32_t entry) {
    return {bytes.begin() + rcf.Offset(entry), bytes.begin() + rcf.Offset(entry) + rcf.Size(entry)};
}
static void WriteMetadata(std::vector<uint8_t>& bytes, const hulk::Rcf& rcf) {
    std::memcpy(bytes.data() + rcf.dir_at, rcf.dir.data(), rcf.dir.size());
    std::memcpy(bytes.data() + rcf.names_at, rcf.names.data(), rcf.names.size());
}
static std::vector<uint8_t> SmallArchive() {
    std::vector<uint8_t> bytes(0xc00);
    auto put = [&](size_t at, uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); };
    std::memcpy(bytes.data(), "ATG CORE CEMENT LIBRARY", 23);
    put(0x24, 0x40); put(0x28, 36); put(0x2c, 0x100); put(0x38, 3);
    // Directory/hash order differs from physical order: entries C,A,B; filename records A,B,C.
    for (unsigned entry = 0; entry < 3; ++entry) {
        put(0x40 + entry * 12, entry + 1);
        put(0x44 + entry * 12, entry ? 0x700 + entry * 0x100 : 0xa00);
        put(0x48 + entry * 12, 1);
    }
    bytes[0x800] = 'A'; bytes[0x900] = 'B'; bytes[0xa00] = 'C';
    size_t pos = 0x108;
    for (const char* name : {"a.bin", "b.bin", "c.bin"}) {
        const auto len = uint32_t(std::strlen(name) + 1);
        put(pos, 123); put(pos + 4, 0x800); put(pos + 12, len);
        std::memcpy(bytes.data() + pos + 16, name, len);
        pos += 16 + len + 3;
    }
    put(0x30, uint32_t(pos - 0x100));
    return bytes;
}
static void CheckLogicalFiles(const std::vector<uint8_t>& bytes, const char* a, const char* b, const char* c) {
    const auto rcf = hulk::ReadRcf(bytes.size(), Reader(bytes));
    if (!Check(rcf.has_value(), "relocated archive reparses")) return;
    const char* names[] = {"c.bin", "a.bin", "b.bin"};
    const char* payloads[] = {c, a, b};
    for (unsigned entry = 0; entry < 3; ++entry) {
        const auto found = rcf->Find(names[entry]);
        if (Check(found && *found == entry, "each filename keeps its original logical entry after relocation"))
            Check(Payload(bytes, *rcf, *found) == payloads[entry], "each filename resolves to its own expected payload");
    }
}
static void RelocationTests() {
    auto bytes = SmallArchive();
    const auto original = hulk::ReadRcf(bytes.size(), Reader(bytes));
    if (!Check(original.has_value(), "original metadata parses")) return;
    for (unsigned pass = 0; pass < 4; ++pass) {
        auto rcf = hulk::ReadRcf(bytes.size(), Reader(bytes));
        if (!Check(rcf.has_value(), "synthetic archive parses")) return;
        const char* name = pass % 2 ? "b.bin" : "a.bin";
        const uint32_t at = uint32_t(hulk::AlignRcf(bytes.size()));
        bytes.resize(at + 1);
        bytes[at] = pass % 2 ? 'B' : 'A';
        rcf->Move(*rcf->Find(name), at, 1);
        WriteMetadata(bytes, *rcf);
        CheckLogicalFiles(bytes, "A", "B", "C");
        const auto reparsed = hulk::ReadRcf(bytes.size(), Reader(bytes));
        if (Check(reparsed.has_value(), "rewritten filename metadata parses")) {
            Check(reparsed->names.size() == original->names.size() &&
                      std::equal(reparsed->names.begin(), reparsed->names.begin() + 8, original->names.begin()),
                  "filename table header and length stay unchanged");
            for (const auto& file : reparsed->files) {
                const auto old = std::find_if(original->files.begin(), original->files.end(),
                                              [&](const auto& f) { return f.entry == file.entry; });
                Check(old != original->files.end() && old->record == file.record,
                      "each logical filename keeps its exact metadata and padding bytes");
            }
        }
    }
}
static std::vector<uint8_t> ServeMods(const std::string& path, const std::vector<uint8_t>& original) {
    auto view = hulk::ModFileView(path, original.size(), Reader(original));
    if (!Check(view != nullptr, "mod archive gets a view")) return original;
    std::vector<uint8_t> bytes(view->size);
    std::memcpy(bytes.data(), original.data(), std::min(bytes.size(), original.size()));
    view->read(0, bytes.data(), bytes.size());
    return bytes;
}
static void ModLayerTests(const char* output) {
    namespace fs = std::filesystem;
    app::test_exe_dir = fs::path(output) / ("mod-rcf-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(app::test_exe_dir / "assets");
    fs::create_directories(app::test_exe_dir / "mods/First");
    fs::create_directories(app::test_exe_dir / "mods/Second");
    std::ofstream(app::test_exe_dir / "mods/First/a.bin", std::ios::binary) << "firstA";
    std::ofstream(app::test_exe_dir / "mods/Second/a.bin", std::ios::binary) << "newA";
    std::ofstream(app::test_exe_dir / "mods/Second/b.bin", std::ios::binary) << "newB";
    hulk::LoadMods();
    auto bytes = SmallArchive();
    for (int layer = 0; layer < 3; ++layer) {
        bytes = ServeMods("D:\\art.rcf", bytes);
        CheckLogicalFiles(bytes, "newA", "newB", "C");
    }
    std::printf("mod regression artifacts: %s\n", app::test_exe_dir.string().c_str());
}

int main(int argc, char** argv) {
    RelocationTests();
    if (argc > 2) ModLayerTests(argv[2]);
    const char* path = argc > 1 ? argv[1] : "assets/misc.rcf";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::printf("SKIP: %s is not here\n", path);
        return failures ? 1 : 0;
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
    hulk::SetFovIncrease(15);
    hulk::SetRenderDistance(250);  // not a choice: Custom
    hulk::SetLanguage(hulk::Language::Spanish);

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
    Check(tables[0].find("TBS_INSTRUCTION_JUMP_CITY\t[Space] Jump to City") != std::string::npos &&
              tables[0].find("TBS_INSTRUCTION_DROP\t[Q] Drop Item") != std::string::npos &&
              tables[0].find("TBS_INSTRUCTION_WEAPONIZE\t[E] Weaponize") != std::string::npos,
          "the HUD's hint lines name the key that does it");
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
        Check(script->find("AddNamedValueInt \"PCFieldOfView\" 3 \"PCFieldOfViewChanged\"") != std::string::npos, "+15 selected");
        Check(script->find("AddNamedValueInt \"PCRenderDistance\" 5 \"PCRenderDistanceChanged\"") != std::string::npos &&
                  script->find("TBS_PC_SCALE_400 TBS_PC_CUSTOM\r\n        NamedValue \"PCRenderDistance\"") != std::string::npos,
              "a render distance outside the choices is Custom");
        Check(script->find("        OnHighlight function \"PCDisplaySync\"\r\n") != std::string::npos, "the selectors sync on highlight");
        Check(script->find("TBS_MENU_ITEM_NO TBS_MENU_ITEM_YES\r\n        NamedValue \"CameraInvertX\"") != std::string::npos &&
                  script->find("TBS_MENU_ITEM_NO TBS_MENU_ITEM_YES\r\n        NamedValue \"CameraInvertY\"") != std::string::npos,
              "Invert X and Y name the camera that looks up with the stick No");
    }
    Check(main_menu.find("AddTextButton \"FEmain_left_item5\"\r\n    {\r\n        text \"TBS_PC_DISPLAY_OPTIONS\"") != std::string::npos &&
              main_menu.find("AddTextSelector \"FEsubMenu_text8\"") != std::string::npos,
          "the main menu's slots");
    Check(main_menu.find("ifplatform PS2 GAMECUBE\r\n{\r\n    AddTextSelector \"FEmain_left_item5\"") != std::string::npos,
          "the PS2 and GameCube widescreen row stays");
    Check(pause_menu.find("AddTextButton \"FEpause_text4\"\r\n    {\r\n        text \"TBS_PC_DISPLAY_OPTIONS\"") != std::string::npos &&
              pause_menu.find("AddTextSelector \"FEpause_text8\"") != std::string::npos &&
              pause_menu.find("        TransitionAnimEnter \"slide_down8\"") != std::string::npos,
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
    Check(kernel::registered.size() == 9, "eight selector callbacks and the sync");
    if (kernel::registered.size() == 9) {
        Check(kernel::registered[1].first == "PCResolutionChanged", "named as the script names them");
        Check(kernel::registered[1].second == std::vector<uint32_t>({0x409B40, 0x101, 0xBBEC0}), "functor through the shared handler");
        Check(kernel::registered[6].first == "PCRenderDistanceChanged", "the render distance's");
        Check(kernel::registered[7].first == "PCLanguageChanged", "the language's");
        Check(kernel::registered[8].first == "PCDisplaySync", "the sync");
        Check(kernel::registered[8].second == std::vector<uint32_t>({0x409B30, 0x108, 0xB46B0}), "sync through Print");
    }
    Check(!hulk::DisplaySync(ctx, 0x00123456), "a real Print owner goes to the game");
    ctx.ecx = 0xABCD;
    Check(hulk::DisplaySync(ctx, 0x108), "the sync");
    Check(ctx.esp == 0x80000 && ctx.ecx == 0xABCD, "the sync leaves the registers as they were");
    Check(kernel::widgets == std::vector<std::string>({"pc_display_mode_selector", "pc_resolution_selector",
                                                        "pc_frame_rate_selector", "pc_vsync_selector", "pc_render_scale_selector",
                                                        "pc_fov_selector", "pc_render_distance_selector",
                                                        "pc_language_selector"}),
          "each selector found by its name");
    Check(kernel::selections == std::vector<std::pair<uint32_t, uint32_t>>({{0x40010, 1}, {0x40020, 3}, {0x40030, 6}, {0x40040, 0}, {0x40050, 2},
                                                                                 {0x40060, 3}, {0x40070, 5}, {0x40080, 2}}),
          "each set to the current setting");
    Check(!hulk::DisplayCallback(0x00123456, 1), "a real AudioQuality owner goes to the game");
    Check(hulk::DisplayCallback(0x101, 5), "resolution selector");
    Check(app::settings.width == 3840 && app::settings.height == 2160, "3840 x 2160 chosen");
    Check(hulk::DisplayCallback(0x100, 2) && app::settings.display_mode == app::DisplayMode::Windowed, "windowed chosen");
    Check(hulk::DisplayCallback(0x101, 99) && app::settings.width == 3840, "a selection past the choices is ignored");
    hulk::TickDisplaySettings();
    Check(app::applied == 0, "changes wait to settle");
    // the view's settings apply at once, without the window's
    const uint32_t generation = hulk::ViewGeneration();
    Check(hulk::DisplayCallback(0x105, 5) && hulk::GetFovIncrease() == 30 && hulk::ViewGeneration() != generation,
          "+30 field of view applies at once");
    Check(hulk::DisplayCallback(0x106, 2) && hulk::GetRenderDistance() == 200, "200% render distance applies at once");
    Check(hulk::DisplayCallback(0x107, 3) && hulk::GetLanguage() == hulk::Language::Italian, "Italian chosen");
    Check(app::applied == 0, "neither applies the window");
    std::vector<uint8_t> after = served();
    std::string again(after.begin(), after.end());
    Check(again.find("AddNamedValueInt \"PCResolution\" 5 \"PCResolutionChanged\"") != std::string::npos &&
              again.find("AddNamedValueInt \"PCDisplayMode\" 2 \"PCDisplayModeChanged\"") != std::string::npos,
          "the script read again starts at the settings as they are");
    Check(again.size() == rcf.size(), "the archive keeps its size");

    // The real misc archive also keeps all logical names after the display view relocates its files.
    const auto original_names = hulk::ReadRcf(own.size(), Reader(own));
    const auto moved_names = hulk::ReadRcf(after.size(), Reader(after));
    if (Check(original_names && moved_names, "display archive can be reparsed by filename")) {
        for (const auto& file : original_names->files) {
            const auto found = moved_names->Find(file.name);
            if (!Check(found && *found == file.entry, "display relocation keeps every filename's entry")) continue;
            if (moved_names->Offset(*found) == original_names->Offset(file.entry))
                Check(Payload(after, *moved_names, *found) == Payload(own, *original_names, file.entry),
                      "unmodified logical files retain their payloads");
        }
    }
    if (argc > 2 && original_names) {
        const auto entry = original_names->Find("frontend\\fe_real.txt");
        if (Check(entry.has_value(), "real frontend exists for mod/display stacking")) {
            const auto mod = app::test_exe_dir / "mods/Second/frontend";
            std::filesystem::create_directories(mod);
            std::ofstream(mod / "fe_real.txt", std::ios::binary) << Payload(own, *original_names, *entry) << "\r\n# relocation marker\r\n";
            hulk::LoadMods();
            const auto bytes = ServeMods("D:\\misc.rcf", own);
            const auto parsed = hulk::ReadRcf(bytes.size(), Reader(bytes));
            if (Check(parsed.has_value(), "mod followed by display view reparses")) {
                for (const auto& file : original_names->files)
                    Check(parsed->Find(file.name) == std::optional<uint32_t>(file.entry), "stacked mod/display keeps every name association");
                const auto text = Payload(bytes, *parsed, *entry);
                Check(text.find("# relocation marker") != std::string::npos && text.find("AddScreen \"display_options\"") != std::string::npos,
                      "relocated mod script and display menu both survive in the correct file");
            }
        }
    }
    auto settle = [] { std::this_thread::sleep_for(std::chrono::milliseconds(450)); hulk::TickDisplaySettings(); };
    settle();
    Check(app::applied == 1, "batched actual mode/resolution change applies once");
    hulk::DisplayCallback(0x100, 0); settle();
    Check(app::applied == 2, "enter fullscreen still applies its window geometry");
    const int applies = app::applied, saves = app::saved;
    for (const auto [owner, choice] : {std::pair{0x102u, 2u}, {0x103u, 1u}, {0x104u, 1u}}) {
        hulk::DisplayCallback(owner, choice); settle();
        Check(app::applied == applies, "FPS/vsync/scale-only change does not reset fullscreen");
    }
    Check(app::saved == saves + 3, "live rendering changes are still saved");
    hulk::DisplayCallback(0x100, 2); hulk::DisplayCallback(0x100, 0); settle();
    Check(app::applied == applies, "mode changed back before settling does not reset fullscreen");
    hulk::DisplayCallback(0x101, 1); settle();
    Check(app::applied == applies + 1, "actual fullscreen resolution change still applies");
    hulk::DisplayCallback(0x101, 5); hulk::DisplayCallback(0x101, 1); settle();
    Check(app::applied == applies + 1, "resolution changed back before settling does not reset again");
    hulk::DisplayCallback(0x104, 1); settle();
    Check(app::applied == applies + 1, "selecting unchanged live settings never resets the mode");


    // the view's arithmetic: degrees added to the game's horizontal field, the planes scaled
    hulk::SetFovIncrease(20);
    Check(std::fabs(hulk::AdjustFov(1.5f) - (1.5f + 20.0f * 3.14159265f / 180.0f)) < 1e-5f, "20 degrees wider");
    Check(std::fabs(hulk::AdjustFov(2.9f) - 170.0f * 3.14159265f / 180.0f) < 1e-4f, "never past 170 degrees");
    Check(hulk::AdjustFov(3.0f) == 3.0f, "a field already past the limit stays the game's");
    hulk::SetFovIncrease(1000);
    Check(hulk::GetFovIncrease() == hulk::kMaxFovIncrease, "the increase is clamped");
    hulk::SetFovIncrease(0);
    Check(hulk::AdjustFov(1.3f) == 1.3f, "no increase leaves the game's field");
    hulk::SetRenderDistance(300);
    Check(hulk::AdjustDistance(650.0f) == 1950.0f, "300% triples a plane");
    hulk::SetRenderDistance(5);
    Check(hulk::GetRenderDistance() == hulk::kMinRenderDistance, "the distance is clamped");
    hulk::SetRenderDistance(100);
    Check(hulk::AdjustDistance(3000.0f) == 3000.0f, "100% leaves the game's planes");

    // the camera distance: a wheel notch is 12%, the camera glides to it
    hulk::SetCameraDistance(100);
    hulk::TickCameraDistance(0.0f);
    Check(hulk::AdjustCameraDistance(12.0f) == 12.0f, "100% leaves the game's distance");
    Check(hulk::StepCameraDistance(-1) && hulk::GetCameraDistance() == 89, "a notch closer");
    Check(hulk::StepCameraDistance(2) && hulk::GetCameraDistance() == 112, "two notches farther");
    Check(hulk::AdjustCameraDistance(12.0f) == 12.0f, "the camera has not moved yet");
    hulk::TickCameraDistance(1.0f / 60.0f);
    const float glide = hulk::AdjustCameraDistance(100.0f);
    Check(glide > 100.0f && glide < 112.0f, "one frame later it is on its way");
    for (int i = 0; i < 120; i++) hulk::TickCameraDistance(1.0f / 60.0f);
    Check(std::fabs(hulk::AdjustCameraDistance(100.0f) - 112.0f) < 0.01f, "two seconds later it is there");
    hulk::SetCameraDistance(hulk::kMaxCameraDistance);
    Check(!hulk::StepCameraDistance(1), "no farther than the farthest");
    hulk::SetCameraDistance(hulk::kMinCameraDistance);
    Check(!hulk::StepCameraDistance(-1), "no closer than the closest");
    hulk::SetCameraDistance(1000);
    Check(hulk::GetCameraDistance() == hulk::kMaxCameraDistance, "the camera distance is clamped");
    hulk::Language l;
    Check(hulk::LanguageFromName("French", l) && l == hulk::Language::French && !hulk::LanguageFromName("german", l),
          "language names");

    std::printf(failures ? "%d failed\n" : "display menu: all passed\n", failures);
    return failures ? 1 : 0;
}
