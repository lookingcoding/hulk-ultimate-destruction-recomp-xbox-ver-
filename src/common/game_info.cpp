#include "app/app.h"
#include "common/addresses.h"
#include "common/camera.h"
#include "common/diagnostics.h"
#include "common/display_menu.h"
#include "common/keyboard.h"
#include "common/mods.h"
#include "common/release.h"
#include "common/settings_ui.h"
#include "common/view.h"
#include "core/input.h"
#include "kernel/memory.h"

// SDL3, linked into the executable for the runtime's controllers (input::Init opens it before boot)
extern "C" bool SDL_HasGamepad(void);

#ifdef HULK_USA
constexpr const char* kExe = "HulkRecompiledUSA";  // the North American release
#else
constexpr const char* kExe = "HulkRecompiled";     // the European release
#endif

namespace {
// whose button prompts the string tables carry: settings.toml's button_prompts, or by what is plugged in
void ApplyPrompts(bool controller) {
    switch (hulk::GetPromptDevice()) {
        case hulk::PromptDevice::Keyboard: hulk::SetControllerPrompts(false); break;
        case hulk::PromptDevice::Controller: hulk::SetControllerPrompts(true); break;
        default: hulk::SetControllerPrompts(controller); break;
    }
}

void OnInit() {
    hulk::InitDiagnostics();
    hulk::RunMatchingRelease();  // the other release's game in assets: its own executable runs it
    hulk::LoadMods();            // mods\ beside assets (mods.h)
}

void OnImageLoaded() {
    hulk::KeepCameraInvert();  // Camera Options' Invert X/Y as the player set them, not inverted at first
    // button prompts: a controller's when one is plugged in, before the game reads its strings
    const bool has_pad = SDL_HasGamepad();
    ApplyPrompts(has_pad);
    hulk::SetGamepadDetected(has_pad);
}

void OnWindowReady(void* hwnd) {
    hulk::SetGameWindow(hwnd);
}

void OnPad(int port, input::Pad& pad) {
    hulk::TickDisplaySettings();
    hulk::KeepCameraInvert();
    if (port == 0) {
        ApplyPrompts(pad.connected);  // a physical controller; keyboard input joins after
        hulk::SetGamepadDetected(pad.connected);
    }
    hulk::KeyboardPad(port, pad);
}

bool OnMessage(void* hwnd, uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result) {
    // the mouse wheel in play: the camera closer (up) or farther (down), gliding a kCameraZoomStep a notch (view.h)
    constexpr uint32_t kMouseWheel = 0x020A;
    constexpr int kWheelDelta = 120;
    if (message == kMouseWheel && hulk::InGameplay()) {
        static int accumulated = 0;
        accumulated += int16_t(uint16_t(wp >> 16));
        int steps = 0;
        for (; accumulated >= kWheelDelta; accumulated -= kWheelDelta) steps--;
        for (; accumulated <= -kWheelDelta; accumulated += kWheelDelta) steps++;
        if (steps && hulk::StepCameraDistance(steps)) hulk::NoteSettingChanged();
    }
    return hulk::KeyboardMessage(hwnd, message, wp, lp, result);
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
    .images = {{"default.xbe", kExe}},               // each recompiled xbe and its exe, for title launches
    .options = {},                                   // game settings kept in the settings file
    .load = hulk::LoadCustomSettings,                // reads more from the settings file's toml::table
    .save = hulk::SaveCustomSettings,                // writes more into it
    .init = OnInit,
    .loaded = OnImageLoaded,                         // image in memory, before its entry point runs
    .window = OnWindowReady,                         // the game window is up
    .message = OnMessage,
    .ui = nullptr,                                   // display settings are a screen of the game's own Options
    .pad = OnPad,
    .rumble = nullptr,                               // changes a rumble before it is set
    .file = hulk::ModFileView,                       // mods, and misc.rcf with the Display Options screen
};
