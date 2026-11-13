# Issue 12: cheats menu investigation

A targeted menu is not implemented yet. The display menu is a frontend-only settings bridge: `src/common/display_menu.cpp` overlays `frontend\\fe_real.txt`, `frontend\\fe_run.txt` and language tables in an in-memory view of `misc.rcf`; registered frontend callbacks write host settings. This does not itself expose gameplay objects or Lua execution.

The checked-in `config/game.toml`, `config/game_usa.toml`, and `src/common/addresses.h` do not identify the player health, adrenaline or unlock structures. An earlier mission-specific experiment at `D:/Projects/Build Cache/Hulk Recompiled/scratch/issue10-audit2/ab-v045-20261106-185622/mods/Manual Upgrades/art/missions/c03m03/c03m03.lua` uses `am_SetActorInvulnerable`, `am_SetActorInvincible`, and `am_SetCharacterHealth` on the named Hulk actor, repeating on a timer; it also uses `um_SetUnlockableLocked` and `um_SetUnlockableOn`. The project note records that this experiment observed invulnerability and 99/99 upgrades, but this is mission-specific evidence, not proof of a general toggle. Its unlock writes may affect progression and saves.

The shipped frontend contains a `CheatAllMoves` named value and `CheatAllMovesChanged` callback (see existing `D:/Projects/Build Cache/Hulk Recompiled/scratch/issue10-audit2/manual-cheat-api-evidence.txt`); whether the release callback is usable and its save effects remain unverified. No safe address, ownership, ABI, lifecycle or region mapping was established for a host-controlled cheat. Infinite adrenaline, one-hit destruction and super jumps have no identified implementation here.

Next, reverse engineer and validate the game's existing cheat callbacks or a gameplay-thread Lua invocation bridge for both executable regions. Establish player creation/destruction, restart, mission transitions, toggling off, and save behavior before wiring the existing display-menu style selectors and persistent settings. Test the overlay and settings headlessly, then test actual effects and reversibility in both regions using disposable saves. A broader cheats menu also needs individual behavior and progression semantics established for each cheat.

## Shared cheat interface (issue-12-cheats-menu, for the movement branch)

`src/common/cheats.h` is the contract between the cheats menu (this branch) and the movement cheats
(`issue-12-cheats-movement`). Build against it; do not edit it on the movement branch. If a cheat needs another
setting, list it in the movement report and it is added here at merge time.

- `enum class hulk::cheats::Cheat`: `Invulnerability, InfiniteRage, InfiniteHealth, Damage, NoHeat, GameSpeed`
  (this branch) and `SuperJump, LowGravity, SuperSpeed` (movement branch).
- Every cheat is a level, `0` = off. `SuperJump`, `LowGravity` and `SuperSpeed` are off/on toggles (levels 0 and 1);
  the strength used at level 1 is the movement code's choice.
- `CheatState`: a snapshot of every level; `state[Cheat::SuperJump]`, `state.On(Cheat::LowGravity)`.
- settings.toml keys (written by `settings_ui.cpp`): `cheat_invulnerability`, `cheat_infinite_rage`,
  `cheat_infinite_health`, `cheat_damage` (0-3), `cheat_no_heat`, `cheat_game_speed` (0-3), `cheat_super_jump`,
  `cheat_low_gravity`, `cheat_super_speed`. Toggles are bools, levelled cheats integers; missing means off.
- Appliers: `using Applier = void (*)(const CheatState&, X86Context& ctx, uint8_t* base)`.
  `RegisterApplier(name, fn)` from a static initializer; `game/simulation.cpp` calls `RunAppliers` on the guest's
  update thread after every world update step (60 Hz accumulator steps and the critical-move slow-motion path),
  always, also when every cheat is off. An applier must put the game back when its cheat is off, find the player
  again on every call (the world is remade between missions, on restart and in the frontend), and use a copy of
  `ctx` for any guest call (`ctx.esp` is the update's caller's stack after the update returned).
- Movement entry point: `void hulk::cheats::ApplyMovement(const CheatState&, X86Context&, uint8_t* base)`, defined
  in `src/game/cheat_movement.cpp` and registered there, for example
  `static const bool registered = hulk::cheats::RegisterApplier("movement", &hulk::cheats::ApplyMovement);`.
  Region addresses go in `src/common/cheat_movement_addresses.h` under `#ifdef HULK_USA` like `addresses.h`.
- Game sources are compiled per region: `src/game/*.cpp` for HulkRecompiled (EU) and `src/game_usa/*.cpp` for
  HulkRecompiledUSA. The movement branch also adds the two-line `src/game_usa/cheat_movement.cpp`
  (`#include "game/cheat_movement.cpp"`), as `src/game_usa/hooks.cpp` does for hooks.
- Menu labels, the Options -> Cheats screen and its four languages belong to this branch (`display_menu.cpp`).

### Interface update: the disc's own cheats and low gravity

The disc already implements several cheats as unlockables that gameplay code checks with `IsUnlockableOn`
(thiscall manager, id; EU `0x929E0`, USA `0x92940`). The unlockable names are in a table at EU `0x49EFF0`
(`0x2FE` entries). `game/cheat_stats.cpp` hooks `IsUnlockableOn` and answers "on" only at the gameplay call sites
(matched by the guest return address) while the player has the host cheat on. The profile's own bits are never
written; the save serializer (EU `0x8B81D`) and the frontend read the same function from other call sites and keep
seeing the disc's values.

- `LowGravity` is the disc's `CHEATS_LOW_GRAVITY` (`0x13`), checked per update at a character's gravity multiplier
  (EU return `0x826AA`, USA `0x826CA`) and the physics world's gravity (-500 instead of -3000; EU `0x1B6024`,
  `0x1B6093`; USA `0x1B5DE4`, `0x1B5E53`). It is answered by this branch. The movement branch must not hook
  `IsUnlockableOn` (one strong definition per function) and should not add a second low gravity; it may read
  `state.On(Cheat::LowGravity)` if a movement cheat needs to know.
- `Cheat::Regeneration` was appended after `SuperSpeed` (settings key `cheat_regeneration`).
