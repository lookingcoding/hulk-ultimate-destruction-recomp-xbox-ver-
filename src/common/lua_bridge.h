#pragma once
#include <cstddef>
#include <cstdint>

// The game's own Lua 5.1 (src\common\lua_bridge.cpp, docs\lua-mods.md). The ScriptHandler singleton owns one
// lua_State and every script the game runs is a chunk loaded through luaL_loadbuffer, which parses source text as
// well as bytecode. This bridge runs a chunk of the host's on that same state, so a mod sees the whole engine script
// API (the am_*/hud_*/sm_* names, lua-research\script-api.txt) with no DLL, no archive edit and no guest code of our
// own. The addresses are the ones static analysis found (addresses.h); they have not been proven in game yet.
//
// Everything runs on the guest's update thread, from OnTick: that is the one place the game's VM is settled, and a
// guest call from another thread is refused (-1). The state is re-read on every call because ScriptHandler::Init
// closes the old state and makes a new one between missions, so nothing here may be cached across calls.
namespace hulk::lua {

uint32_t State();  // the live lua_State*, 0 when the game has none yet (or the handler is gone)
bool Ready();      // State() != 0: there is a VM to run a chunk on

// runs a chunk of Lua 5.1 source on the game's state. Returns 0 when it ran, 1 when it did not compile, 2 when it
// raised an error, -1 when it did not run at all (no state, not on the update thread, or the VM faulted). The error
// message of a 1 or 2 is LastError(). 'name' is the chunk name Lua reports in errors ("@mods/My Mod/main.lua").
int Run(const char* src, size_t len, const char* name);
int Run(const char* src, const char* name);  // the same, the length from strlen

// evaluates a Lua expression on the game's state: "hud_Message" is not needed, but "sm_GetVar('x')" is.
// GetNumber: false when it did not run or its value is not a number (nil, a table, a function). Numbers are the
// game's lua_Number, a 32-bit float, widened into *out.
bool GetNumber(const char* expr, float* out);
// GetString: the value as a Lua string (numbers converted), false when it is nil or not convertible.
bool GetString(const char* expr, char* buf, int cap);

const char* LastError();  // the last failing Run/GetNumber/GetString, "" when there is none

}  // namespace hulk::lua
