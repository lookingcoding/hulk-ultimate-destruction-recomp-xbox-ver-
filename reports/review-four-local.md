# Four review fixes — local candidate

2026-11-06. Based on `03e5829e4531c8ab42c5863970a45d470b8e6244` (v0.4.5). All changes remain local and uncommitted. No push, release, deployment or GitHub message. v0.4.5 is still **not a confirmed fix for Frost's truck**.

## Changes and independent checks

### Archive filename identity

`src/common/rcf.h`, `rcf.cpp`, `mods.cpp`, `display_menu.cpp`:

- An RCF filename record corresponds to a logical hash-directory entry, but records are serialized in physical file-offset order. Moving only directory offsets made the next parse associate names with the wrong entries.
- `ReadRcf` now retains each complete original filename record with its logical entry. `Rcf::Move` reorders these records to match the updated offsets (and size/entry tie ordering), preserving the record metadata, padding, names-table header and total length.
- Both mod and Display Options views overlay the corrected filename table as well as the directory. Display Options relocations use the same `Move` operation.
- The header/name offsets and hash entries stay unchanged. Retail archives are not edited on disk.

`tests/display_menu.cpp` actually relocates files, reparses four times and verifies **every name's original logical entry and expected payload**, not just the edited entry. It also verifies byte-for-byte per-name metadata/padding and filename-table header/length. A filesystem-backed test loads two ordered mods (including an override), applies three successive mod layers and checks all logical names/payloads. A real `misc.rcf` test relocates a modded frontend script, stacks Display Options on it and checks every name association plus both edits in the correct script. The existing real-archive hash/header/unmodified-payload checks still pass.

### Display resets

`src/common/display_menu.cpp`:

- Only mode/resolution selections request window application. FPS cap, vsync and render scale use their existing live settings paths and are saved without `ApplyDisplay`.
- A snapshot of the last requested mode/width/height prevents a reset when a geometry selection changes back before the debounce expires.
- Actual fullscreen/windowed and resolution changes still apply. The SDK's mode-switch code was not changed; the unnecessary calls into it were removed.

Automated tests run the real callbacks and debounce path: actual batched geometry changes apply once; fullscreen entry and a real fullscreen resolution change apply; each live-only change saves without applying the window; unchanged selections and mode/resolution round trips do not apply it again. DX11/DX12 present paths already read current vsync, render scaling reads current render percentage, and pacing reads current FPS cap.

### Delete with a controller

`src/common/keyboard.cpp`, `tests/keyboard.cpp`:

- Menu Delete maps to X independently of mouse suppression when a gamepad is connected. Mouse suppression and controller cursor behavior remain separate.
- Tests cover Delete press/release with a connected pad, preserving other controller actions and physical X, no leftover/duplicate X after release, focus loss clearing the key, and Delete remaining unbound to gameplay attack.
- The keyboard test target mocks cursor clipping (`HULK_KEYBOARD_HEADLESS_TEST`) so its null-HWND unit tests do not alter the desktop cursor. Production cursor behavior is unchanged.

### World/combat timing

`src/common/simulation.h`, `src/game/simulation.cpp`, USA include `src/game_usa/simulation.cpp`:

- Positive-time world updates retain the bounded fixed 60 Hz accumulator.
- Zero-time world housekeeping now runs at 60 Hz based on monotonic wall time, with **zero simulation delta**. It cannot bypass to one world call per rendered frame. This preserves the controller/camera work needed during paused cinematics without advancing physics time.
- A thread-local, unwind-safe tick scope makes the original elapsed getter return the tick's delta during world work: 1/60 for ordinary ticks, zero for paused housekeeping. The combat `FrameReachedCondition` that reads this getter therefore sees simulation time. Other callers retain the original clock. EU getter `0x000860A0`; matched USA getter `0x000860C0`.
- States 6/7 retain the original separate non-world branch. Catch-up calls reconstruct both guest arguments, and both the arguments and clock scope restore on unwind.

`tests/simulation.cpp` links the **actual world/getter wrappers** with stub originals for both regions. It checks simulation totals at different render rates, zero-time housekeeping frequency at 60/120/144, zero delta in both the argument and getter during housekeeping, positive fixed delta, original getter outside ticks, argument reconstruction/restoration, four-tick catch-up, invalid elapsed values, fractional-time resets, non-world branches and exception restoration. Test wall time is injected deterministically; production uses `steady_clock`.

**Correction to the outside-callback lead:** original EU `1D1B80` does not dispatch timed callbacks. Its full body loads the Lua state, calls `32B6B0` with argument zero, then returns. The zero branch clears the Lua value stack; it neither invokes a callable nor updates actor/prop transforms. That specific routine cannot directly move the truck, so it was left unchanged. This corrects the earlier description; it is not a claim that every outside-world routine in the engine has been exhaustively classified. The unrelated outside audio-manager chain was not moved speculatively.

`src/game/vehicle_probe.cpp` adds opt-in mission-state logging through diagnostic queries for a nonexistent actor. The original lookup/zero-vector behavior is retained. `src/common/replay.cpp` now checks exact foreground HWND ownership and PID on **every input poll**, logging and blocking attempts when the target game is not foreground.

## Build and automated validation

Final command (PowerShell; existing helper initializes VS/LLVM and directs temporary files to D:):

```powershell
& 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-build.ps1' *> 'D:\Projects\Build Cache\Hulk Recompiled\scratch\review-four\build-final.log'
```

Its build/test commands are:

```text
cmake --build "D:\Projects\Build Cache\Hulk Recompiled\release" --parallel 4
ctest --test-dir "D:\Projects\Build Cache\Hulk Recompiled\release" --output-on-failure
```

**PASS:** final log records links for EU and USA executables and game DLLs. All six CTests pass (5.18 seconds): `simulation_render_independence`, `simulation_usa_wrapper`, `vtable_method_discovery`, `keyboard_input_lifecycle`, `kernel_io_completion`, `display_menu_rcf`. Real retail `misc.rcf` was present; the archive tests were not skipped. `tools/Build.ps1` includes the new USA wrapper test in its pre-CTest build targets. `git diff --check` passes; Git emits normal LF/CRLF conversion notices.

Local outputs (folder name `release` is the development output, not a published release):

- `D:\Projects\Build Cache\Hulk Recompiled\release\HulkRecompiled.exe` and `.dll`
- `D:\Projects\Build Cache\Hulk Recompiled\release\HulkRecompiledUSA.exe` and `.dll`

Final SHA256 manifest: `scratch/review-four/build-final-hashes.json`. Build/test transcript: `scratch/review-four/build-final.log`; detailed test output: `release/Testing/Temporary/LastTest.log`. Filesystem regression fixtures are under `release/test-data/mod-rcf-*` and their exact paths appear in the test output.

## Native results and explicit limits

Native interactive testing is **paused by the user's later instruction**. No more launches, focus changes, manual-focus requests or input attempts are authorized in this turn.

- **Failed intermediate implementation, corrected:** simply skipping all zero-time world calls froze the mission camera. That trial is `scratch/issue10-timing-fix/combat-60`; its last mission query is at about 42 seconds despite continued rendered frames. It is not a pass or the final implementation.
- **Earlier partial 60-cap timing candidate:** after replacing the skip with clocked zero-delta housekeeping, `paused-combat-60` logs approximately 60 zero-delta movement calls per second during the camera interval, then ordinary positive-delta updates and mission queries resume. Queries continue past 138 seconds. It remained in `retrieve_truck_chase`; a controlled test-only health reduction to 1200 did not establish a natural shield break or pickup. This predates the integrated archive/display/Delete build and the foreground gate, and is not a final-build three-cap combat validation.
- **Integrated guarded 60-cap attempt:** `guarded-combat-60` logs zero-delta housekeeping followed by resumed world updates and actor/smart-prop position queries. Its controller replay recorded **20 blocked events and zero foreground-verified input events**. It cannot count as a combat/shield-input test. The fixture's scripted placements/queries are not a normal campaign playthrough. No runtime error lines were logged in these timing attempts.
- **NOT RUN / NOT VERIFIED:** final-build combat/shield contact at 60/120/144, stable-144 gameplay, natural shield break, pickup/carry/drop, delivery and full campaign; USA gameplay; native physical-monitor mode transitions and native Delete with a physical controller. The automated callback and simulated-pad regressions above pass. No claim that Frost's truck is fixed is supported.
- No new blackout reproduction or confirmation. No full-engine fixed-timestep/interpolation rewrite.

Window discovery/activation prerequisites failed: the Sky tool reported `Computer Use native pipe is unavailable: failed to connect native pipe: The system cannot find the file specified. (os error 2)`; the other interface's state discovery reported `failed to write kernel assets: The system cannot find the path specified. (os error 3)`. No activation/input was performed through either interface. This was a tool failure, not an approval denial. The subsequent manual-focus request was superseded by the user's stop instruction; nothing is pending for focus/permission.

## Shutdown and preservation

Stopped the remaining task game PID **15516**, after matching its exact path:

`D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-timing-fix\guarded-combat-60\HulkRecompiled.exe`

The first immediate process enumeration raced process termination; a second check confirmed **zero task-owned game processes and zero task replay runners**. Associated three-cap runner had already been stopped. Unrelated processes were not stopped. All computer controls ceased. No game was relaunched.

Original user save SHA1 is still `89C3AF3A6B854BA8386C8E54AB31D5D467FD8B83`. Retail assets, standalone installed game, original saves, Windows settings and public releases were not changed. Existing SDK working-tree patches remain untouched; no SDK gitlink update. The earlier follow-up report is retained.

Additional evidence under `scratch/review-four`: `build.log`, `build-hashes.json`, `native-summary.json`, `outside-lua-wrapper.asm`, `lua-zero-stack-path.asm`. Timing runners, mod Lua, isolated logs/HDDs/captures and earlier build logs remain under `scratch/issue10-timing-fix`; no cleanup/deletion of evidence. The native three-cap script is a saved fixture only and is not running.
