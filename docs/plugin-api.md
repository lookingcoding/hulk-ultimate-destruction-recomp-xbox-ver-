# Script plugins

Script/trainer mods in C or C++, GTA-style: the game loads every `mods\*.dll` and `mods\*.asi`,
and every `mods\<Mod>\*.dll` and `*.asi`, before it boots. A plugin gets the game's own state and
code through a small C ABI, ticks once per world update step, and is kept away from the game when
it misbehaves: a fault in a plugin is caught, logged, and after three the plugin is disabled, while
the game carries on. Nothing on disk changes and the game runs without a `mods` folder as it is.

- [plugins\hulk_plugin.h](..\plugins\hulk_plugin.h) is the whole API, a standalone C header: include
  it, export `HulkPluginQuery`, done.
- [plugins\example_trainer.cpp](..\plugins\example_trainer.cpp) is a working trainer to start from.

## Installing

Put the plugin's `.dll` (64-bit) in the `mods` folder beside the game's `assets` folder:

    Hulk Recompiled\
      assets\
      mods\
        ExampleTrainer.dll        one plugin, a single file
        My Mod\
          Trainer.dll             one plugin in its own folder, beside its data files
          data\...                whatever else the plugin keeps, it finds it itself
        _Old Trainer.dll          a file whose name starts with _ is left out
        _WIP\                     so is a folder whose name starts with _ or .

That is the same `mods` folder the file mods use (see its `README.txt`), so one folder can carry
both. The plugins load in name order, after `plugins::Load` runs at boot (`src\common\plugins.cpp`,
called from `game_info.cpp`), and unload when the game's window closes. A relaunch — the game
handing over to its other disc's executable — replaces the process without unloading, and the OS
reclaims the modules with it.

## The API

`plugins\hulk_plugin.h` is self-contained C: include it from C or C++.

```c
#include "hulk_plugin.h"

static void HULK_PLUGIN_CALL OnLoad(HulkHostApi* host) { /* keep host */ }
static void HULK_PLUGIN_CALL OnTick(float dt) { /* once per world update step */ }
static void HULK_PLUGIN_CALL OnUnload(void) { }

const HulkPluginInfo* HulkPluginQuery(void) {
    static const HulkPluginInfo plugin = {
        HULK_PLUGIN_API_VERSION, "My Mod", "me", "1.0", "what it does",
        &OnLoad, &OnTick, &OnUnload,
    };
    return &plugin;
}
```

`HulkPluginQuery` is the one export. `api_version` must be `HULK_PLUGIN_API_VERSION` (1, the
header's), or the plugin is not loaded and the log says what it was built for. `name` shows in the
log; the file name stands in when it is not there. `author`, `version` and `description` are
optional. `OnLoad` runs once at boot, before the game's entry point, with the `HulkHostApi*`;
`OnTick(float dt)` runs on the guest's update thread, after each world update step, with the step's
dt (`hulk::g_world_dt`, `src\common\simulation.h`); `OnUnload` runs when the game's window closes.
A null callback is skipped, so a mod that only ticks leaves `OnLoad` and `OnUnload` out.

### What the host API does

| Function | What it is |
| --- | --- |
| `Log(fmt, ...)` | the game's log, `logs\hulk.log` |
| `GetHulkPos()` / `SetHulkPos(pos)` | the player's position; the setter is ignored when he has no matrix |
| `GetHealth()` / `SetHealth(h)` | his power; the setter goes through the game's own, so a dead character stays dead |
| `GetMaxHealth()` | the most his power can be |
| `GetRage()` / `SetRage(r)` | the power above his Critical Mass threshold, 0 below it; the setter puts him in Critical Mass, up to the maximum |
| `SetGameSpeed(scale)` | the world clock's scale, 1 is the game's own; clamped to 0.01..100 |
| `ReadMem(va, dst, n)` / `WriteMem(va, src, n)` | the game's memory at a guest address; every page the range touches must be committed, or it returns 0 |
| `CallGuest(fn, args, count)` | the game's own code at a guest address, its arguments on the stack; OnTick's thread only |

Addresses are the guest's own, what `src\common\addresses.h` (`hulk::addr`) and the code the
recompiler wrote use: `kApp` is the app, `kSetHealth` the health setter, `kTimerSetScale` the world
clock's setter. The tables the recompilation found are in `config\game.toml`; the offsets this API
reads are the ones the game's hooks verified — the power at the character's `+0x1B0C/10/14`, the
position in the spatial's matrix. `CallGuest` and the setters must run from `OnTick`, the one place
the game's update thread is settled; from `OnLoad` they are refused with a note in the log.

A plugin DLL is a Windows DLL and may use Windows' own: the example's keys come from
`GetAsyncKeyState`. What the host API covers is the game, not the PC.

### Faults

Every call into a plugin is wrapped in SEH (`src\common\plugins.cpp`). The first fault is logged
with its code, the third disables the plugin — its `OnTick` is no longer called and it is unloaded
without calling `OnUnload` again — and the game carries on without it. `OnLoad` and `OnUnload` are
wrapped the same way, so a plugin that faults at boot is disabled before it can hurt the game.

### Building

From a Visual Studio x64 command prompt (or `tools\Build.ps1`'s environment), beside the header:

    clang++ -std=c++20 -O2 -shared example_trainer.cpp -o ExampleTrainer.dll

or MSVC's own:

    cl /std:c++20 /O2 /LD example_trainer.cpp /link /EXPORT:HulkPluginQuery

MSVC's 32-bit linker names a cdecl export `_HulkPluginQuery`; the host looks the undecorated name
up first and the decorated one second, but build 64-bit regardless: the game is 64-bit, and a
32-bit DLL does not load into it.

## Where things are

- `plugins\hulk_plugin.h` — the API a plugin is written against
- `plugins\example_trainer.cpp` — the example trainer
- `src\common\plugins.h` / `plugins.cpp` — the host: the scan, the load, the tick, the unload
- `src\common\game_info.cpp` — `plugins::Load` at boot, `plugins::Unload` on the window's close
- `src\common\simulation.h` / `src\game\simulation.cpp` — `g_world_dt` and the `plugins::Tick` calls
- `src\common\cheat_stats.cpp` — the offsets the health, rage and speed functions use
- `src\game\cheat_movement.cpp` — the position offsets the movement audit verified
