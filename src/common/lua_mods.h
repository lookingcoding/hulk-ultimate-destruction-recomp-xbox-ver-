#pragma once
#include <cstdint>

// Plain-text Lua mods (docs\lua-mods.md): mods\*.lua and mods\<Mod>\lua\*.lua run on the game's own Lua 5.1
// (common/lua_bridge.h), once each time a fresh lua_State appears — a new mission, a restarted game — and a script
// that defines a global OnTick(dt) then runs again on every world update step, through a pcall, on the guest's
// update thread. Nothing a script does is fatal here: one that does not compile is logged and skipped, and one whose
// OnTick keeps failing is disabled with its error in the log. A background test runs none of them.
namespace hulk::lua_mods {

void Tick(float dt);  // each world update step, on its thread (game/simulation.cpp, beside plugins::Tick)

}  // namespace hulk::lua_mods
