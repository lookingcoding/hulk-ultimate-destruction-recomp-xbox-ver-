// Example Trainer: the smallest script plugin there is (docs\plugin-api.md). It builds into
// ExampleTrainer.dll, a 64-bit Windows DLL beside the game's exe, and shows the whole API:
//
//   clang++ -std=c++20 -O2 -shared example_trainer.cpp -o ExampleTrainer.dll
//
// from a Visual Studio command prompt, or from build.bat's environment. Drop it in mods\ (or
// mods\My Mod\, beside its own data files), start the game, and:
//
//   F5  full health      F6  Critical Mass    F7  toggles half speed
//
// The keys come from GetAsyncKeyState, Windows' own, which a plugin DLL may call directly: the host
// API covers the game, not the PC. Everything else goes through the HulkHostApi OnLoad hands over.
#include <windows.h>
#include <cmath>
#include "hulk_plugin.h"

namespace {

HulkHostApi* host = nullptr;  // OnLoad's, good for the plugin's whole life

// one entry per key the trainer answers to, its edge tracked between the ticks
struct Key {
    int code;
    bool was;
};
Key kKeys[] = {{VK_F5, false}, {VK_F6, false}, {VK_F7, false}};

bool Pressed(Key& key) {
    const bool down = (GetAsyncKeyState(key.code) & 0x8000) != 0;
    const bool pressed = down && !key.was;
    key.was = down;
    return pressed;
}

void HULK_PLUGIN_CALL OnLoad(HulkHostApi* api) { host = api; }

void HULK_PLUGIN_CALL OnTick(float dt) {
    (void)dt;
    if (Pressed(kKeys[0])) {  // F5: the power back to its maximum
        host->SetHealth(host->GetMaxHealth());
        host->Log("[trainer] full health");
    }
    if (Pressed(kKeys[1])) {  // F6: Critical Mass, as the infinite-rage cheat holds it
        host->SetRage(1.0e9f);  // clamped to the maximum the power can be
        host->Log("[trainer] full rage");
    }
    if (Pressed(kKeys[2])) {  // F7: half speed until it is toggled back
        static bool slow = false;
        slow = !slow;
        host->SetGameSpeed(slow ? 0.5f : 1.0f);
        host->Log("[trainer] game speed %.2f", slow ? 0.5f : 1.0f);
    }
}

void HULK_PLUGIN_CALL OnUnload() { host = nullptr; }

}  // namespace

extern "C" HULK_PLUGIN_EXPORT const HulkPluginInfo* HulkPluginQuery(void) {
    static const HulkPluginInfo plugin = {
        HULK_PLUGIN_API_VERSION,  // the host checks this first
        "Example Trainer",
        "a modder",
        "1.0",
        "F5 full health, F6 Critical Mass, F7 toggles half speed",
        &OnLoad,
        &OnTick,
        &OnUnload,
    };
    return &plugin;
}
