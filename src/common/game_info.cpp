#include "app/app.h"
#include "common/keyboard.h"
#include "common/diagnostics.h"
#include "common/settings_ui.h"

namespace {
void OnWindowReady(void* hwnd) {
    hulk::SetGameWindow(hwnd);
}
}  // namespace

// What the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"The Incredible Hulk: Ultimate Destruction",               // window and splash
    .short_title = L"Hulk Recompiled",               // message boxes
    .name = L"HulkRecompiled",                      // window class, crash dumps
    .log = "hulk",                                   // release/logs/hulk.log beside the exe
    .settings = "settings.toml",                     // default settings next to the exe
    .splash = L"assets/splash.png",
    .missing = L"Please provide either the retail disc image or the extracted game files in the assets directory.\n\n"
               L"Supported disc files:\n"
               L"  assets/Incredible Hulk, The - Ultimate Destruction.iso\n"
               L"  assets/default.xbe and game data archives\n",
    .images = {{"default.xbe", "HulkRecompiled.dll"}},
    .options = {},
    .load = hulk::LoadCustomSettings,     // load settings from settings.toml
    .save = hulk::SaveCustomSettings,     // save settings to settings.toml
    .init = nullptr,                      // before any guest code runs
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = OnWindowReady,              // the game window is up
    .message = hulk::KeyboardMessage,
    .ui = hulk::DrawNativeDisplayUi,      // 1:1 native-styled display settings UI (no ImGui windows)
    .pad = hulk::KeyboardPad,
    .rumble = nullptr,                    // changes a rumble before it is set
};
