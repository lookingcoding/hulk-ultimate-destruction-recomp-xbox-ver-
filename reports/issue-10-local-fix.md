# Issue #10 local fixes â€” 2026-11-06

Implementation and local validation for v0.4.5. This report records testing before publication; the user subsequently authorized committing, pushing, publishing, and replying on issue #10. The standalone installation and user saves have not been replaced. Follow-up to [the display-idle investigation](issue-10-power.md).

## Display idle prevention

`patches/zzzzzz-display-awake.patch` updates the shared SDK native window loop (`sdk/runtime/app/main.cpp`), covering both regional executables and both renderers:

- Request `ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED` on the window thread while active, shown and not minimized. Clear on deactivation, hide, minimize, destroy, boot failure and normal shutdown. Thread/process termination also releases Windows' thread-scoped request.
- Track activation/show/minimize notifications independently. `WM_SHOWWINDOW` precedes the visible-style change, so querying `IsWindowVisible` from that event gave the wrong answer in an initial test; this was corrected.
- Log request acquisition/release and failures, activation, resolution changes, system power broadcasts, and `GUID_CONSOLE_DISPLAY_STATE` notifications (on/dim/off). Register/unregister the display notification with the game window. Existing DX11/DX12 device-removal diagnostics remain separate.
- Do not alter any Windows power plan, display timeout, driver or fullscreen mode. The mode remains a native fullscreen popup with optional resolution switching. No screensaver policy override.

This fixes the confirmed missing keepawake mechanism. It does **not** prove that Frost's blackout is an idle timeout rather than another graphics/monitor fault. This laptop is plugged in with display timeout disabled; no actual display-off cycle was induced. API semantics: [SetThreadExecutionState](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate), [display idle criteria](https://learn.microsoft.com/en-us/windows/win32/power/system-sleep-criteria), [execution-state query](https://learn.microsoft.com/en-us/windows/win32/api/powerbase/nf-powerbase-callntpowerinformation).

## Truck timing correction

Fetched [Frost's seventh comment](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-/issues/10#issuecomment-6693722723), 2026-11-06T19:38:30Z, confirming clean 60 fps and broken 120/144. Local phase tracing reproduced ascent after transitioning from distant sleepwalk movement to active simulation. During the rise, the ground-correction wrapper itself reported zero net vertical displacement; this was not simply an elevated patrol route.

The root coupling is the legacy active world pipeline being invoked once per rendered frame. Under old timing, active movement phases were observed around 140â€“145 calls/second with roughly 7 ms steps at the high cap. Some original physics paths already multiply by elapsed time; replacing every dt with 1/60 at 144 invocations would speed the game up, and changing only integration would leave actor/prop/collision phases inconsistent. The precise individual force/contact equation behind the upward feedback has **not** been isolated. The correction removes render-frequency dependence at the existing ordered simulation boundary, without rewriting that solver.

`src/common/simulation.h`, `src/game/simulation.cpp`, and the USA include in `src/game_usa/simulation.cpp` introduce a 60 Hz accumulator around EU `0x00088CD0` / USA `0x00088C30`. These functions match instruction for instruction after code/data addresses are masked. The existing order remains: agents/actors, smart props, physics, actor post-collision, mission/camera/scene work. Drawing runs outside that boundary (EU `0x001B9CC0` / `0x001BD100`).

The wrapper consumes elapsed time in 1/60-second ticks, performs at most four catch-up ticks per render (matching the original clock's approximately 1/15-second stall bound), and carries only fractional time. It preserves the original non-world branches for states 6/7 and resets fractional time at ownership/pause transitions. Both guest arguments and the entry context are reconstructed on each catch-up call: the original function overwrites its argument slots with camera temporaries. The original slots are restored on normal return/unwind.

This is a simulation-scheduling change, not a new global rendering cap or a truck-height clamp. It affects the shared world update, not just one truck. **World animation/camera/motion updates are consequently 60 Hz; interpolation between simulation states has not been added.** Higher render caps remain available. A later interpolation layer would need previous/current actor, prop, camera and skeletal transforms, with teleport/pickup/cutscene discontinuity handling; it is a separate, larger change.

## Test setup and limitations

All native truck runs use the European disc, DX11, 640Ã—480, 50% render scale, vsync off, and isolated test HDDs/mod folders under the D: build cache. No truck transform/physics values are patched by the test mod. The fixture places Hulk beside the truck once at 25 seconds to exercise close approach. A periodic half-max-health refill avoids fresh-profile Critical Mass/Adrenaline Surge prompts and preserves contact/damage events. This is a scripted proximity test, not a normal playthrough. A legacy-timing control uses the identical health/placement fixture, so the difference is not attributable solely to that refill.

Opt-in European diagnostics in `src/game/vehicle_probe.cpp` log actor position and movement/ground/sleepwalk phase frequency/dt. Normal runs do not enable the probe. Actor-root samples are not continuous body-transform/video measurements. Captures and compilation can stall performance; clean high-cap repeats omit renderer captures and avoid compilation during the affected road segment. FPS values are per-second averages, not per-frame guarantees.

The first fixed-timing run that refilled to full health triggered a Critical Mass tutorial before the truck spawned and was aborted. It is not a passing truck test. The initial 120-cap captured run also suffered insufficient FPS around the important road segment and was repeated. A final catch-up ABI correction restores both guest argument slots; the clean 144 repeat and low-rate smoke use that correction.

### Later mission stages

A 210-second 144-cap stage fixture attempted stepped truck health, a pickup input and a return jump. Captures still showed the disable-truck objective; pickup was not observed. The return locator's Z convention was wrong for the actor-position API and put Hulk outside the level. That is a **test-fixture failure**, not an original mission or runtime physics edit. No shield-break, carry/drop or delivery completion pass is claimed. No USA gameplay or Frost hardware testing, and no full campaign validation.

## Automated checks and build outputs

Both European and USA executable/DLL builds succeed under `D:\Projects\Build Cache\Hulk Recompiled\release`. Five CTests pass: vtable discovery, keyboard lifecycle, kernel I/O completion, display menu/archive handling, and the new simulation-render-independence test. The latter checks equal simulated time at 30/60/120/144 input rates, irregular frame times, bounded stalls, pause/reset and invalid elapsed values. It does not substitute for native mission tests.

No assets, user saves, installed game settings, disc files, Windows settings or release packages were changed. All test state and runner scripts are under `D:\Projects\Build Cache\Hulk Recompiled\scratch`. Existing applied SDK patches are preserved; the new keepawake patch passes reverse-apply validation.

## Evidence locations

- Source investigation: `scratch\issue10-vehicle-re.py`, `issue10-classes.py`, `issue10-vtable.py`, `issue10-xrefs.py`, `issue10-floats.py`, `issue10-match-usa.py`, and `issue10-vehicle-re\*.asm`. The USA world-update comparison matches after masking relocated addresses.
- Native runs and corresponding `.ps1` runners: `issue10-diag-144`, `issue10-phase-144`, `issue10-fixed-144` (aborted tutorial), `issue10-fixed2-144`, `issue10-fixed-120`, `issue10-fixed-60`, `issue10-baseline-health-144`, `issue10-fixed-clean-120`, `issue10-fixed-clean-144`, and `issue10-stages-144`. Each isolated folder holds its exe/DLL, settings, assets junction, mod entry/args, logs, test HDD and result. Captures only where configured.
- Stage fixture: `scratch\issue10-stage.lua`, `issue10-stage.input`, five renderer captures under `issue10-stages-144\captures` (151359, 151424, 151439, 151504, 151539).
- Analysis: `scratch\issue10-fix-analyze.py`, `issue10-fix-results.json`, `issue10-fix-chart-template.html`, `issue10-fix-chart.py`, `issue10-fix-height.html`.
- Power harness: `scratch\issue10-power-lifecycle.py`; earlier show/minimize/restore evidence in `issue10-fixed2-144\power-lifecycle.json`. A fixture foreground transfer initially failed to take focus; it is not counted as a successful alt-tab test. Final harness records actual foreground ownership and read-only system execution flags.
- Build logs: `scratch\issue10-build-power.log`, `issue10-build-trace.log`, `issue10-build-simulation.log`, `issue10-build-power-lifecycle.log`, `issue10-build-final.log`, `issue10-build-callframe.log`. The first incremental helper attempts failed on missing vswhere PATH / PowerShell's native-stderr handling; corrected in D: `issue10-build.ps1`. Final builds/tests succeed.

## Measured native results

All road-test entries below are 150-second EU/DX11 runs with the same health/placement fixture. Y is the named actor's world coordinate in game units; it is not a unit conversion to metres. Maxima describe sampled positions, not every frame.

| Timing / render cap | Complete actor samples | Y range | Median FPS after 30 s | FPS in matching road segment (median; range) | Runtime outcome |
|---|---:|---:|---:|---:|---|
| Legacy / 144 | 74 | 0–11.234 | 144.0 | 144.0; 141.8–144.3 | Rise reproduced; no errors/early exit |
| Fixed / 60 | 102 | -0.026–0.321 | 60.0 | 59.95; 5.5–60.6 (capture stall) | Stayed near road; no errors/early exit |
| Fixed / 120 clean | 92 | -0.021–0.317 | 120.0 | 120.0; 119.9–120.1 | Stayed near road; no errors/early exit |
| Fixed / 144 clean | 89 | -0.022–0.321 | 144.0 | 143.9; 133.4–144.3 | Stayed near road; no errors/early exit |

The matching road filter is X -1600..-1460 and Z -1660..-1530, using the min/max times of matching truck samples and one-second margin for FPS observations. These are not perfectly locked, uninterrupted per-frame rate guarantees. The earlier captured fixed 120 trial had only a 65.3 fps median in this segment and was not used as the clean 120 validation. The earlier captured 144 trial also had dips; the clean repeats are the relevant high-cap evidence. In fixed positive-dt steady-state phase logs, the simulation uses 0.016667-second updates and roughly 60 active movement calls/second. Paused zero-dt callbacks are separate.

### Final DX12/fullscreen power and catch-up smoke

`issue10-power-dx12`: fullscreen at desktop resolution, DX12, 30 fps cap, 25% render scale. At about 17 seconds telemetry shows 30.0 rendered fps and 62 movement/ground calls in a roughly one-second observation, with 0.016667 dt: the low-rate path exercises multiple simulation ticks per rendered frame. No runtime errors. The harness intentionally closes the window normally at about 38 seconds, so `early_exit=True` in this run is expected, not a crash.

`power-lifecycle.json` records actual foreground ownership plus successful read-only `CallNtPowerInformation(SystemExecutionState)` results. Both display/system flags were 3 while focused, 0 when minimized, 3 after restoring, 0 while the harness's own window was in the foreground, 3 after returning to the game, and 0 after normal close. Corresponding native activation/request on/off logs agree. The initial display-on notification was received. Actual display dim/off notifications, an idle-timeout cycle, physical keyboard Alt-Tab, and a GPU reset were not induced; this tests equivalent real focus transfer to an owned test window and normal minimize/restore/exit. No global Windows settings changed.

### Build-script integration

`tools/Build.ps1` now includes `hulk_simulation_tests` in the pre-CTest tools target list, so a fresh build does not try to run a test executable it has not built. The normal `Build.ps1 -Stage Tools` path validated all applied patches and passed all five tests after this change (`issue10-build-script-check.log`). Both regional executable/DLL targets were built after the catch-up call-frame correction (`issue10-build-callframe.log`).

### Tutorial regression smoke

The existing `tools/Test-Game.ps1` / `tests/tutorial.input` replay ran for 195 seconds on the final EU/DX12 build, 144 cap, 640×480 /50% scale, with a fresh isolated `hdd2` and no mission mod. It reached gameplay, did not exit early, and logged zero errors. Nineteen renderer captures were produced. Inspected captures at 152540 show the intro, 152650/152700 show Hulk moving in the tutorial, and 152730 shows grounded gameplay with the run-around prompt. This is a basic startup/input/movement/rendering smoke, **not** completion of the tutorial or validation that every scripted pickup/throw/punch succeeded. Files: `scratch\issue10-tutorial-smoke.ps1`, `issue10-tutorial-smoke-result.txt`, and `issue10-tutorial-smoke\` (copied final exe/DLL, assets junction, settings, hdd2, logs and captures).

The comparison chart initially rejected a missing `valueFormat.style`; corrected to `decimal` and desktop/mobile previews then rendered without console errors. The chart-check helper's python3 launcher remains unavailable on this workstation (known Microsoft Store alias), so no CLI checker pass is claimed. Calculations use the saved parsed telemetry JSON. Final repository diff whitespace check passes (Git prints normal CRLF conversion notices). All test game processes are stopped.
