#include "app/app.h"
#include "common/diagnostics.h"
#include "common/display_menu.h"
#include "common/keyboard.h"
#include "common/settings_ui.h"
#include "core/input.h"
#include "kernel/memory.h"

// SDL3, linked into the executable for the runtime's controllers (input::Init opens it before boot)
extern "C" bool SDL_HasGamepad(void);

namespace {
void OnImageLoaded() {
    // Camera Options' Invert X/Y flags (0x004DF97A, 0x004DF97B) start zeroed, which the game shows and plays as both
    // axes inverted; a save keeps the player's own choice and restores it over this (0x0008AA91)
    if (uint8_t* base = mem::Base()) base[0x004DF97A] = base[0x004DF97B] = 1;
    // button prompts: a controller's when one is plugged in, before the game reads its strings
    hulk::SetControllerPrompts(SDL_HasGamepad());
}

void OnWindowReady(void* hwnd) {
    hulk::SetGameWindow(hwnd);
}

void OnPad(int port, input::Pad& pad) {
    hulk::TickDisplaySettings();
    if (port == 0) hulk::SetControllerPrompts(pad.connected);  // a physical controller; keyboard input joins after
    hulk::KeyboardPad(port, pad);
}
}  // namespace

// What the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"The Incredible Hulk: Ultimate Destruction",               // window and splash
    .short_title = L"Hulk Recompiled",               // message boxes
    .name = L"Hulk Recompiled",                      // window classes, crash dumps, handover event
    .log = "hulk",                                   // log file prefix
    .settings = "settings.toml",                     // settings file beside the exe
    .splash = L"splash",                             // optional splash.png or splash.bmp beside the exe
    .missing = L"No working copy of the game was found. Put your Xbox disc image of The Incredible Hulk: Ultimate "
               L"Destruction (.iso, any file name) in the assets folder, or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "HulkRecompiled"}},   // each recompiled xbe and its exe, for title launches
    .options = {},                                   // game settings kept in the settings file
    .load = hulk::LoadCustomSettings,                // reads more from the settings file's toml::table
    .save = hulk::SaveCustomSettings,                // writes more into it
    .init = hulk::InitDiagnostics,
    .loaded = OnImageLoaded,                         // image in memory, before its entry point runs
    .window = OnWindowReady,                         // the game window is up
    .message = hulk::KeyboardMessage,
    .ui = nullptr,                                   // display settings are a screen of the game's own Options
    .pad = OnPad,
    .rumble = nullptr,                               // changes a rumble before it is set
    .file = hulk::GameFileView,                      // misc.rcf with the Display Options screen in its frontend
};
