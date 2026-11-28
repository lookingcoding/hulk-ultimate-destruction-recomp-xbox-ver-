# Lua mods

Plain-text `.lua` files are read from the `mods` folder and run on **the game's own Lua 5.1**, the VM the
disc's scripts run on. No DLL, no compiler and no archive edit: a script sees the engine's script API
(`am_*`, `hud_*`, `sm_*`, `cm_*`, … — 712 names, the ones the shipped scripts call), and one that defines
`OnTick(dt)` is called after every world update step.

    Hulk Recompiled\
      assets\
      mods\
        LongJump.lua              a script in the mods folder itself
        Sandbox\
          lua\
            main.lua              every *.lua in a mod's lua folder
            hud.lua
        _WIP.lua                  a name starting with _ or . is off
        _Old Mod\                 so is a folder that starts with _ or .
          lua\old.lua

Scripts run in name order, the same `mods` folder the file mods and the plugins use. A background test
(`hulk::BackgroundTest`) runs none of them.

## When a script runs

- **Once per Lua state.** The game makes a fresh `lua_State` when its `ScriptHandler` is initialised
  (`ScriptHandler::Init` closes the old one), which is what a mission load or a restarted game does. Every
  script is run again on the new state, so a script that sets up per-mission state belongs there. The state is
  re-read on every call and matched by pointer, so nothing is ever held across a reset.
- **Every world update step, if it defines a global `OnTick(dt)`.** The call is a Lua `pcall` on the update
  thread, after the world's own update, with the step's dt — the same thread and moment a script plugin's
  `OnTick` gets (`src\common\lua_mods.cpp`, called from `src\game\simulation.cpp` beside `plugins::Tick`).

```lua
-- mods\Sandbox\lua\hello.lua
local elapsed = 0

function OnTick(dt)
    elapsed = elapsed + dt
    if elapsed < 5 then return end
    elapsed = 0
    hud_Message('Sandbox is still running', 1.5)   -- a real engine call, from a shipped script
end
```

A script with no `OnTick` still runs once per state — that is the place for one-shot work such as unlocking
something or spawning a prop.

## Errors, and what is fatal

Nothing a Lua mod does ends the game.

| what happened | what the game does |
| --- | --- |
| the file is empty, unreadable, or bigger than the bridge's page (about 255 KiB) | logged, the script is skipped |
| the chunk does not compile | logged with Lua's message, the script is skipped |
| the chunk raises an error | logged with Lua's message, the script is skipped |
| `OnTick` raises an error | logged; after three in a row the script's `OnTick` is no longer called |
| any of the above | the rest of the game, and the other scripts, carry on |

Look in `logs\hulk.log` for `lua mods:` lines; the first one names each script and whether it ticks.

## The environment

Whether the engine's script names are globals of the state's `_G` or only of a table it keeps in the Lua
registry (`HC_REGISTRY`) is not something a mod has to know: the host asks once per state, and when the names
are not in `_G` it puts this prologue in front of every chunk (`src\common\lua_bridge.cpp`):

```lua
local r = debug.getregistry()
local ctx = r.HC_REGISTRY
local env = r.HULK_ENV or setmetatable({}, {__index = function(_, k)
    local v = ctx[k]; if v ~= nil then return v end; return _G[k]
end})
r.HULK_ENV = env
setfenv(1, env)
```

So a chunk reads the engine's names first and everything else from `_G`. Two consequences worth knowing:

- Globals a script sets live in that environment (or in `_G`, when the names are there already). They are
  shared between Lua mods, but not with the scripts the game loads from the disc.
- A script that wants the real global table can still write to it as `_G.name = value`.

## Limits

- **Numbers are 32-bit floats.** The engine's `lua_Number` is a float, so arithmetic is single precision and
  precompiled Lua chunks from elsewhere do not match. Plain source text is not affected.
- **`print()` goes nowhere useful.** It writes to the CRT's stdout, not `logs\hulk.log`. Use an engine call
  such as `hud_Message`, or raise an error to get a line in the log.
- **The game's functions expect the game's arguments.** The names come from `script-api.txt` in the issue's
  research folder (`D:\Projects\Build Cache\hulk-issue23\lua-research\`), where the shipped scripts show the
  argument lists. A wrong argument list can fault the engine; that fault is caught (the update thread survives),
  and the run reports an error, but keep an eye on `logs\hulk.log` while writing a script.
- **A tick runs inside the game's frame.** Keep `OnTick` short; there is no coroutine trick that gets around
  the update thread.
- **The addresses are from static analysis, not from a run in game.** See "Where things are" and
  `src\common\addresses.h`; if the bridge cannot answer, every call reports -1 and the game runs normally.

## Where things are

- `src\common\lua_bridge.h` / `lua_bridge.cpp` — `State()`, `Ready()`, `Run()`, `GetNumber()`, `GetString()`,
  `LastError()`: the host side of the game's VM
- `src\common\lua_mods.h` / `lua_mods.cpp` — the `mods` scan, the once-per-state run, and the per-step
  `OnTick` pcall
- `src\common\addresses.h` — `kScriptHandler`, `kLuaLoadBuffer`, `kLuaPcall`, … for both releases
- `src\game\simulation.cpp` — where each world update step ends up calling `lua_mods::Tick`
- `docs\plugin-api.md` — the same bridge as `RunLua`/`LuaReady` for a DLL plugin (API v2)
