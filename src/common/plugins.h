#pragma once
// The script plugin API modders write against (plugins\hulk_plugin.h): this header is its host side.
#include "../../plugins/hulk_plugin.h"

// Script plugins (plugins\hulk_plugin.h, docs\plugin-api.md): mods\*.dll and *.asi, and
// mods\<Mod>\*.dll and *.asi, are loaded before the game boots (game_info.cpp), ticked on the
// guest's update thread after each world update step (game\simulation.cpp) and unloaded when the
// game's window closes. A plugin whose query is missing, or built for another API version, is left
// out with a note in the log; one that faults is caught, logged and disabled after repeated faults
// instead of ending the game. A background test skips them entirely.
namespace hulk::plugins {

void Load();          // once at init, before the game boots; skipped for a background test
void Tick(float dt);  // each world update step, on its thread
void Unload();        // the game's window is closing

}  // namespace hulk::plugins
