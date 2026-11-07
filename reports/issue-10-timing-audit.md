# Issue 10 timing audit — local investigation

2026-11-06. Base: `03e5829e4531c8ab42c5863970a45d470b8e6244` (published v0.4.5). The existing four-review changes are preserved. Nothing committed, pushed, released or posted to GitHub. **Frost's flying truck is not confirmed fixed.**

## Demonstrated defects addressed

1. **Mixed zero/positive deltas could starve the world scheduler.** `WorldSimulationClock::Advance` discarded fractional time whenever the zero/nonzero mode changed. The exact alternating-frame, ten-second regression produced 300 active ticks at 60 FPS but zero at 120/144. Separate active and housekeeping accumulators now retain their own fractional budgets. The regression produces 300 active and 300 zero-delta housekeeping ticks at every tested cap. Explicit resets still clear both. Housekeeping does not advance simulation time.
2. **The scoped elapsed getter affected unrelated clock objects.** The original method reads the receiver's scaled frame duration at `+0x30/+0x34`. The override now requires a non-null receiver matching the main world clock at `[kApp]+0x58`. Distinct paused/scaled clock objects keep their own values, including zero and 1/240 second. Tests execute the actual EU/USA wrappers.
3. **A thread-local scope could leak across native fibers.** All these fibers share one kernel thread. The binding is now suspended immediately around `SwitchToFiber` and restored when that exact continuation resumes. The original guest register/stack switch is unchanged. A Win32 native-fiber regression failed before the change and passes with it: a sibling sees its original clock, the parent sees its fixed delta on return, and the child resumed after the parent's tick sees the original clock. Nested scope/unwind tests remain covered. Native audit logs also show switches with `tick_active=1` at guest caller `002D5FE5`, so switches during this scope are real. A truck movement/combat dependency on those switched callbacks is **not established**.
4. **Paused helpers still consumed a render delta at a 60 Hz call rate.** Baseline hidden runs showed one zero-time cinematic interval lasting approximately 5.7 seconds at 60 FPS, 11.25 at 120 and 14 at 144. EU `1C0EF0` explicitly falls back to the main clock's unscaled getter `85F60` when the supplied simulation delta is near zero. That getter reads `+0x20/+0x24`; it is a different method from the scaled elapsed getter. During zero-time housekeeping only, the main-clock raw getter now supplies 1/60 second of unscaled wall time. The scaled getter and world argument remain zero. Other receivers, reads outside the scope, and positive/scaled-time raw reads retain the original behavior. USA `85F80` was independently disassembled and has the matching field reads. This is a camera/cinematic preservation fix, not evidence of the truck's cause.

Source: `src/common/simulation.h`, new `src/common/simulation_context.h`, `src/game/simulation.cpp`, `src/game/fibers.cpp`; USA includes the shared implementation. Regression coverage is in `tests/simulation.cpp`. The old render-swap comment in `src/game/hooks.cpp` no longer asserts that all simulation is measured-time driven.

## Static findings and limits

- Corrected `paths.py` from `match[0] == 0xe8` to `match[0][0] == 0xe8`. Raw byte scans are only candidate discovery. `validate_paths.py` decodes from recovered function starts and checks the precise instruction address, kind and target. **49/49 selected candidates validate: 48 calls and one tail jump.** Artifacts: `validated-paths.json` and `path-*.asm`. This is not an exhaustive indirect-call graph.
- The generated-instruction direct-call graph finds the stream-service path `87680 → 8DBA0 → 2E4370 → 2E4280 → 2E39F0 → 2D5FD0 → 350EBD`. It does not find a direct world-to-fiber route. Runtime active-scope switches show why absence of a direct path cannot rule out indirect reachability. Script/combat continuation ownership remains to be traced.
- A register-tag scan enumerated candidate main-clock `+0x30/+0x34` accesses. After narrowing clock-method seeds to verified methods, its 14 field-access candidates are inside the elapsed getter and clock updater, including writes. No extra inline main-clock elapsed consumer was found by this scan. It is linear alias analysis, not proof that none exists. In particular, CApp methods `862C0/862E0` also access offsets 30/34 on a different receiver type; those are not evidence of clock reads.
- Distinct methods remain important: `85F60` is unscaled frame time, `86020` unscaled accumulated time, `86060` scaled accumulated time, `860A0` scaled frame time, and `86110` the Q16 time scale. Raw-time consumers include `1C06D0/1C0EF0/1C5D10` and `EC580`. Their positive/scaled-time behavior has not been normalized speculatively.
- EU `1AFDE0`, `1B0190`, `1B1950` read accumulated scaled time and store a timestamp at `+1B88`. The inspected sites do not themselves divide by it or apply a movement impulse. Their consumers and repeated catch-up ticks with an unchanged render-clock timestamp remain a lead, not a proved defect.
- Core native movement/post-collision paths inspected are inside the world pipeline: `88CD0 → 2AB40 → 40950` and sleepwalk `3E270`; `88CD0 → 2AE80 → 42940 → 428C0 → 42660 → 418D0`. No second per-render truck integration path was established in this audit. Indirect/scripted/pickup/carry paths are not exhaustively resolved.
- **One suspected zero-delta divide is guarded.** In `42660`, the displacement-to-velocity division at `427BA` is skipped for absolute delta at/below approximately `1e-5` by the comparison/branch at `42756–42774`. No direct division was found in the inspected `40950`, `418D0`, `3D320` or `3DD40` bodies. This rules out that specific division as an unguarded zero-delta cause; it does not certify every virtual/transitive contact helper.
- The previously investigated outside-world `1D1B80` path is a Lua stack reset, not evidence of another time-based truck update. Prior published-package verification is documented in `issue-10-v045-followup.md`; v0.4.5 really contained its scheduler, so packaging alone does not explain Frost's failure.
- Frost's original screenshot has the red destroy target in the disable/shielded stage. It does not establish pickup/delivery, and the white beam's meaning remains unknown. Root actor height alone is insufficient to rule out a body/visual transform problem; both actor and smart-prop heights are logged here, but collision-body/individual mesh transforms are not continuously captured.

## Background fixture isolation

The user subsequently authorized background tests and an isolated internal-pad fixture. This supersedes the earlier paused-native status in `review-four-local.md`.

`src/common/replay.cpp` enables the fixture only with all of: `HULK_TEST_INTERNAL_PAD=1`, `HULK_TEST_EXE` matching the running executable's absolute path, and a replay file. It writes only the passed `input::Pad` plus the game's capture request. Ordinary replay retains its foreground-window/PID guard. The fixture does not call `SendInput` or desktop keyboard/mouse APIs.

`src/common/game_info.cpp` bypasses KeyboardPad, KeyboardMessage, cursor/window registration and automatic regional relaunch only in this test mode. New `patches/zzzzzzz-internal-pad-test.patch` keeps the SDK render window hidden, suppresses mode/show/focus/splash/handover actions, disables ImGui mouse/cursor handling and suppresses hardware rumble. Normal startup is unchanged when the fixture is absent. `tests/replay.cpp` verifies default/wrong-executable rejection, exact-executable opt-in, port/Pad isolation, capture triggering, and that the internal branch never calls `app::Window`. Its tests create no window.

The runner preflights the exact EU XBE hash, rejects an existing game, copies the tested EXE/DLL into an isolated D: directory, and checks DLL freshness before launch. A read-only EnumWindows/GetForegroundWindow probe verifies only that PID's windows are hidden and the process is not foreground at start/end. Finally it verifies the executable path before stopping its own process. No OS input or activation was attempted. The failed computer-use interfaces were not retried.

## Native scenarios and interpretation

The isolated fixture starts `c03m03` from its level setup, not a campaign save. It places Hulk near the truck at selected times and supplies attack/grab inputs. Health is refilled, and truck HP is artificially changed to 1200 at 90 seconds. This is a targeted contact test, **not a natural shield-break pass**. Logged `zap=2` establishes the shield-contact event fired. Nil shield globals cannot establish shield state. Input acceptance alone does not establish a successful attack, pickup or delivery.

### Completed background runs, before the final cinematic correction

All three used EU/DX11, a hidden 640×480 window, 25% render scale and 190-second sessions. The 60/120 runs included four GPU dumps; the 144 run omitted them to reduce capture stalls. FPS statistics are log samples during seconds 35–185, not continuous per-frame minima. Thus even the no-capture run is **not a sustained locked-144 proof**.

| Cap | Logged FPS min / median / max | Actor and smart-prop Y range | Complete paired position samples | Accepted fixture events |
|---|---|---|---|---|
| 60 | 1.0 / 60.0 / 61.5 | −0.0292 to 0.3216 | 147 | 20, including captures |
| 120 | 0.9 / 120.0 / 120.8 | −0.0127 to 0.2865 | 157 | 20, including captures |
| 144 | 112.7 / 143.95 / 144.7 | −0.0170 to 0.3155 | 161 | 16, no captures |

Each run observed `zap=2`, stayed in `retrieve_truck_chase` after the intro, and had **zero blocked fixture events**. The sampled truck remained near road height. Inputs attempting grab did not establish pickup. No natural shield break, carry or delivery was reached. Movement and ground logs contain dt=0 during housekeeping and dt=0.016667 during ordinary ticks; sleepwalk samples use 0.016667. The audit saw only two zero/nonzero mode transitions in each run, **not** the alternating-per-render pattern that triggers the synthetic starvation regression. That exact mixed-delta bug is therefore proved in code, but not proved to occur in this truck scenario.

Task-owned background processes 2388 (60), 3428 (120) and 15184 (144) were stopped by their runners after exact-path verification. Start/end probes show hidden windows and `foreground_is_test=false`. These baseline runs do not validate the subsequent raw-clock cinematic correction.

### Latest build and manual handoff

The user asked to play the mission personally. No further automated game/replay run was started. A separate, normally visible EU/DX11 session was launched at **1280×720, 100% render scale, 144 FPS cap, vsync off**, using only a mission-start args mod (`-fmvvol 0 -pal -level c03m03`). It contains no Lua test script, health edits, injected controller inputs or replay file. Environment replay/capture switches were removed; only read-only timing/performance diagnostics remain enabled. It uses a fresh isolated HDD directory rather than replacing the user's saves. A direct level start is not proof of loading Frost's campaign/upgrades/profile.

Initial manual session: `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\manual-frantic-20261106-180651\HulkRecompiled.exe`, PID **12344**. The read-only window probe confirmed **visible=true and foreground=true**. The log confirms the args-only mod and zero fixture/replay announcements. No input was sent by the agent. This process was subsequently stopped, after exact-path verification, when the user explicitly requested the cheat-enabled restart below.

Exact manual build (base commit plus local changes, including the new paused raw-clock hook):

- EXE SHA256: `916ACFF2218518BD853E73BF1AC0B5ECF338E7FD6236E033951C2CED99297597`
- DLL SHA256: `0E93F155AC4DC2775F2E2F78BBB80CE44B79380484536C118634476F54D1E870`
- Hash manifest, settings and logs are in the manual session directory; `manual-session.json` is in the audit scratch directory.

The latest camera correction is **built and unit-tested in both regions; its three-cap native transition/duration comparison is still pending**. The manual game's log does confirm the new paused raw getter executes: at 82.630 seconds it supplies 0.016667 for the main clock from callers `000ABCC2` and `001C5D21`. This establishes native binding/reachability, not correct duration for every cinematic. The three-cap native matrix above predates that last correction. The user now controls the game; no competing game, replay or automated control is running. Logged manual FPS samples vary below the configured cap, so no locked-144 claim applies to this visible session.

During manual play the user reported: **“im playing it but.......... well no flying truck”**, then confirmed the stage was **chasing / attacking the shielded truck**. Record this as a user-observed non-reproduction in shielded chase/combat. Do not count natural shield break, pickup or delivery as passed. This does not establish Frost's cause or invalidate his report.

### User-requested cheat-enabled restart

The user then asked to restart the mission with maximum upgrades and invincibility. The same EXE/DLL build is now running as PID **17956** from `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\manual-cheats-20261106-181535`. The prior PID12344 is stopped; no competing automated game/replay is running. The new window was verified visible and foreground, with the same 144 FPS cap/settings. The user retains all controller/keyboard input.

Only this isolated session's mod now overrides `art\missions\c03m03\c03m03.lua`. It preserves the original spawn call, then uses the game's own unlockable APIs over `constants.unlockables.move_1` through `move_last - 1` (the same loop found in retail `init.lua`), enables available corresponding built-in cheats, sets Hulk invulnerable/invincible, and refreshes protection/full health every 0.25 simulation seconds. There is no truck health/position/stage edit and no controller replay. The flags/API argument forms were checked against native binding strings and retail autotester bytecode before use.

**Runtime verified at 8.159 seconds:** `manual max upgrades enabled=99/99 maxHP=2750 invulnerable=1 invincible=1`. No Lua error was found in that startup check. This proves the requested upgrade flags and protection were applied; it does not certify every move animation or scripted mission failure behavior. Original saves and installed settings remain untouched. Subsequent play with these explicit cheats is a different test condition from Frost's ordinary play and the initial clean manual non-reproduction.

Evidence added: `manual-cheats-session.json`; the new session's `mods\Manual Upgrades\{args.txt,art\missions\c03m03\c03m03.lua}`, settings, actual-path hash manifest, logs and `window-ready.json`; `original-c03m03.lua`, `manual-cheat-api-evidence.txt`, and extracted/disassembled retail `cheat-scripts` in the audit scratch directory. The first evidence text write hit a local encoding error and was rerun explicitly as UTF-8; no game asset was changed. No C++ change/build was needed for this user-requested runtime mod.

## Validation, files and commands

Latest build command (VS/LLVM environment and TEMP/TMP directed to D: by the helper):

```powershell
& 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-build.ps1' *> 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\paused-clock-build.log'
# helper runs:
cmake --build 'D:\Projects\Build Cache\Hulk Recompiled\release' --parallel 4
ctest --test-dir 'D:\Projects\Build Cache\Hulk Recompiled\release' --output-on-failure
```

**PASS:** both EU and USA EXE/DLL linked after the final source edits. **7/7 tests pass, 6.71 seconds:** `simulation_render_independence`, `simulation_usa_wrapper`, `internal_pad_fixture`, `vtable_method_discovery`, `keyboard_input_lifecycle`, `kernel_io_completion`, `display_menu_rcf`. The final simulation tests check scaled zero versus unscaled 1/60 in housekeeping, untouched positive-time raw reads, alternate paused/scaled receivers, scope restoration, mixed schedules and native fiber isolation. The archive suite exercises real relocation/reparse/mod/display stacking, not just name lookup on an unchanged archive. Keyboard tests do not clip/move the desktop cursor.

Nonfatal build warning: MSVC CRT marks `getenv` deprecated in the diagnostic helper. Earlier regression runs intentionally failed for the mixed-delta and fiber defects. Fixture bring-up also encountered and corrected a missing log-type include, a duplicate inline GPU global and CRT-versus-Win32 environment API mismatch. These intermediate failures are preserved in the scratch logs; the final passing result is not substituted for them.

Completed background commands:

```powershell
& 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\run_background.ps1' -Fps 60
& 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\run_background.ps1' -Fps 120
& 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\run_background.ps1' -Fps 144 -NoCaptures
python 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\summarize_native.py'
python 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\paths.py'
python 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\validate_paths.py'
python 'D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-audit2\clock_fields.py'
git -c core.safecrlf=false diff --check
```

The last whitespace check passed, and all **12 SDK patch reverse-apply checks pass**, including the new internal-pad patch. `paths.py` prints raw candidates; only `validated-paths.json` carries instruction-boundary validation. The clock-field script is explicitly a candidate scan with the alias limitations above. Full disassemblies are retained under `scratch\issue10-vehicle-re`; its CLI stdout truncates long functions, so conclusions above were checked against saved slices/files, not just stdout.

Local outputs: `D:\Projects\Build Cache\Hulk Recompiled\release\HulkRecompiled.exe/.dll` and `HulkRecompiledUSA.exe/.dll`; PDBs, import libraries, compiler files and unit-test outputs stay in the same D: build cache. Native evidence: `scratch\issue10-audit2\native-{60,120,144}\{logs,captures,window-*.json,result.txt}` and `native-summary.json`. No original game installation, public package, retail archive or original save was overwritten. SDK patch dirt remains local; no gitlink commit.

Root files changed across the preserved four-review work and this follow-up: `CMakeLists.txt`, `STATUS.md`; `src/common/{display_menu.cpp,game_info.cpp,keyboard.cpp,mods.cpp,rcf.cpp,rcf.h,replay.cpp,replay.h,simulation.h}`; new `src/common/simulation_context.h`; `src/game/{fibers.cpp,hooks.cpp,simulation.cpp,vehicle_probe.cpp}`; `tests/{display_menu.cpp,keyboard.cpp,simulation.cpp}` and new `tests/replay.cpp`; `tools/Build.ps1`; new SDK patch and local reports. The new patch changes SDK `runtime/app/{app.h,main.cpp}` and `runtime/core/input.cpp`; other SDK dirt is preserved from existing patches. No source/evidence was deleted or moved.

## What remains and what would distinguish the causes

- **Present in v0.4.5:** its scheduler allowed zero-time world calls outside fixed scheduling, and combat could read render elapsed time. These are plausible high-FPS contributors; neither is a proven complete explanation of Frost's reproduction. Prior close-approach successes did not validate later mission stages.
- **Introduced in the local follow-up candidate:** zero/nonzero accumulator resets, a receiver-blind getter scope, fiber scope leakage, and slow paused cinematic timing. The fixes above correct these local regressions. They must not be described as demonstrated causes in Frost's published v0.4.5 build, which did not contain the new scoped getter/housekeeping implementation.
- **Strong remaining leads:** raw/cumulative clock consumers and indirect callbacks; different shield-contact/attack actions, upgrade/profile state, and body/mesh versus root transforms. No damping, altitude clamp or blanket FPS cap was added.
- **Unverified:** Frost's exact save/hardware/frame sequence; a genuinely uninterrupted 144 FPS contact run; final-candidate natural shield break, pickup/carry/drop/delivery; USA native gameplay; full campaign; physical-controller Delete and physical fullscreen transition flows. The blackout remains unconfirmed on Frost's machine; this investigation adds no evidence that his display problem is solved.
- **Next diagnostics after the user's manual test:** correlate the visible lift's time with actor, smart-prop and body/mesh heights, movement/ground/sleepwalk dt, mission/shield state and the actual FPS samples; trace raw/accumulated clock consumers and callback stacks at that instant. Prefer a copy of the affected campaign save plus a short video of the action that triggers lift, with build/region/FPS cap and upgrade state. Ask the owner before posting any request on GitHub. If the manual test exposes the cinematic transition, verify the paused raw-clock audit caller and real-time duration against the earlier baseline before declaring that regression natively resolved.

No tool denial is pending. Previous computer-use activation failures were not bypassed with desktop input; normal app startup opened the explicitly requested manual game window. The automated retest of the last camera correction is **paused for the user's manual play**, not reported as passed.

## User-reported full mission completion

After playing the cheat-enabled manual session, the user reported: "ok welp i finished the mission peffectly fine". This is user-reported successful completion of Frantic Recovery on the latest local candidate, with maximum upgrades/invincibility enabled and a configured 144 FPS cap. It extends the manual result beyond shielded chase to mission completion. The agent did not observe each shield/pickup/delivery transition separately or capture its precise timing, and actual FPS was variable, so this is not proof of a sustained locked-144 run. It does not establish the original cause or confirm Frost's affected save/machine is fixed. Earlier automated runs still reached only the chase stage. No input, restart, shutdown or publication action was taken in response; the manual game remains under the user's control.

Renderer follow-up: user asked whether DX11 explains the non-reproduction. Read issue10 body/comments via `gh issue view 10 --repo lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver- --json body,comments`; no DX11/DX12/DirectX/D3D11/D3D12/renderer mention was found, so Frost's selected backend remains unconfirmed. The successful manual session log explicitly says renderer=d3d11, Intel UHD Graphics, flip swap chain/tearing allowed. A backend-dependent timing difference remains possible; no renderer cause is established. Next controlled check is the same local build/mission/actions on DX12 with actual FPS and cheat/profile conditions recorded. No renderer setting, game process or controls were changed during this read-only check.

## Second-opinion review (Opus 5.5, 2026-11-06): strongest v0.4.5 lead

**Finding: v0.4.5's 60 Hz scheduler makes combat frame events miss at >60 FPS, with a miss rate that matches Frost's report exactly.** `FrameReachedCondition` (EU `13AD90`, RTTI `FrameReachedCondition@motion@hulkfight` slot `+0x18`) loads the current animation frame `[esi+0x23C]`, computes `next = cur + anim_rate[+0x14] * speed[+0x228] * elapsed`, where `elapsed` is the main clock's scaled *render-frame* time from `860A0` (`13AE43..13AE70`), then fires only if the target frame `[ebp+0x18]` lies in `[cur, next)` (`13AE91..13AEAB`). Under v0.4.5 each world tick advances animation by 1/60 s, but the tested window is only one render frame wide. Consecutive windows therefore leave gaps, and a target frame inside a gap never fires.

Model (`scratch/issue10-audit2/frame_reached_miss.py`, v0.4.5 accumulator reproduced exactly): **30/60 FPS 0% missed, 120 FPS 50%, 144 FPS 59%**, 165 FPS 64%, 240 FPS 75%. Frost's report is "fine at 60, broken at 120 and 144". In v0.4.4 the world ran per render frame with the same dt, so windows were contiguous; this gap was **introduced by the v0.4.5 scheduler** while it fixed the per-render physics coupling.

The binary has only two `call 860A0` sites: the main loop (`8784C`, the world's argument) and this condition (`13AE55`). So in v0.4.5 this is the unique per-tick consumer of render-frame time. The local candidate's `SimulationTickScope` override of `860A0` (USA `860C0`) returns the tick's 1/60 during world ticks, which removes the gaps. That is consistent with the user's clean full-mission completion at a median 143.9 FPS (84% of 1170 perf samples >=130 FPS) on the candidate.

**Not proven:** which hulkfight move's missed frame event lifts the truck (candidates: shield-hit reaction, grab/attach, release/throw, land/snap-to-ground transitions). The model is a uniform-target estimate, not a trace. **Confirming test (not yet run, needs user OK):** same machine, same cheat mod, mission `c03m03`, 144 cap, played on the published v0.4.5 package versus the candidate; optionally log FrameReached hits/misses per cap with a probe on `13AD90`.

Other checks done in this review, all negative: the guest clock is `rdtsc` (`2D03B0 -> 350671`) with a hard-coded 733,333,333 Hz frequency (`350682`), and the host emulates `rdtsc` from `steady_clock` at the same rate, so there is no coarse-timer or zero-elapsed effect on any machine. DLL regeneration after an overlay upgrade is stamped by EXE size/mtime plus XBE SHA1 and re-runs `xbe_recomp`, so a stale v0.4.4 DLL cannot silently bypass the scheduler. FOV/Render Distance hooks touch only the render-layer camera and fog. The `dt=0` 144/s phase entries in `issue10-fixed-clean-144` (32-39 s) are an all-zero intro cinematic, not gameplay. `86A30`'s per-render callees include HUD (`C5310`), not truck physics.

**Secondary lead still open in the candidate:** 26 call sites read accumulated scaled time `86060`, including `43330` beside the actor post-collision code. Accumulated time still advances per render frame, so two catch-up ticks in one frame see identical timestamps (delta 0), and at 144 FPS the timestamp delta between ticks alternates around 13.9 and 20.8 ms against 16.67 ms of simulation. At exactly 120 FPS the delta matches, so this cannot by itself explain a 120 FPS failure.

### A/B manual test, side A (published v0.4.5)

Launched with `scratch/issue10-audit2/ab-launch.ps1 -Build v045`: PID 11016, `ab-v045-20261106-185622`. Same settings, cheat mod (99/99 upgrades, invulnerable/invincible verified at 14.2 s), fresh save and truck/perf logging as the clean candidate session; only EXE/DLL differ. Result pending user play.

### A/B result, side A: published v0.4.5 did NOT reproduce

User played the v0.4.5 session (PID 11016, `ab-v045-20261106-185622`, exited cleanly at 194 s) and reported: "it ddidnt fly". The log shows a real 144 FPS run: median 144.0, 92% of 184 perf samples >=130. The same machine, settings and cheat mod therefore show no visible lift on **either** v0.4.5 or the candidate. The A/B **cannot confirm the FrameReached hypothesis**. The miss mechanism is real in code, but on this machine at 144 FPS it did not produce a flying truck through this play style. That weakens it as the explanation for Frost's report. It remains possible that only a particular move/frame event Frost uses matters.

**Probe defect found (affects interpretation of all phase logs):** `vehicle_probe.cpp` caches `truck_character` from the first by-name query and never revalidates it. The cheat mod queried only once (134 s). After a 3.4 s gap with no truck callbacks (139.1-142.5 s, during resource loading), the traced actor reports y = 147.935 with zero velocity, then falls and climbs at about +/-21 units/s with sleepwalk plus 1/15 s LOD movement until 158 s. The clean candidate run reports the identical 147.935 at 179 s. An identical value to four decimals across two builds points to a scripted spawn into a reused actor slot (a different actor), not truck physics. Treat every `truck phase` sample after the truck is removed or replaced as **unverified identity**. Fix before relying on it: re-query by name every second (`am_GetActorPosition(DivisionTruck)`) and drop the cache when the by-name result changes or disappears.

No code changed. No push, release or GitHub post.

User asked to question Frost; posted https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-/issues/10#issuecomment-6699217924 (text in scratch/issue10-audit2/frost-question.md). Awaiting his stage/action answer.
