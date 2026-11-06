# Issue #10: display-idle investigation (2026-11-06)

Local investigation only. No runtime fix implemented, build published, or issue reply posted.

## Reporter evidence

Fetched comment [6692809590](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-/issues/10#issuecomment-6692809590), created 2026-11-06T18:53:02Z, the sixth comment. Frost confirms both symptoms persist in v0.4.4 and suspects the screen sleeps after ten minutes. Therefore the v0.4.4 DX12 descriptor correction did not resolve his observed blackout. This does not establish a driver timeout or an idle timeout as its cause.

## Confirmed source findings

- `sdk/runtime/app/game_code.cpp:717,732` requests `ES_CONTINUOUS | ES_SYSTEM_REQUIRED` only during first-launch compilation, then clears it. It does not request `ES_DISPLAY_REQUIRED`. No corresponding gameplay display-required request was found in runtime or game source.
- `sdk/runtime/app/main.cpp:81` applies display settings. Fullscreen and borderless both use a native `WS_POPUP` covering the monitor. Fullscreen at a different configured resolution optionally calls `ChangeDisplaySettingsExW(... CDS_FULLSCREEN ...)`. At desktop resolution no mode switch is performed.
- Both DX11 and DX12 use windowed DXGI swap chains (null fullscreen description; the older DX11 fallback explicitly sets `Windowed=TRUE`). No runtime `SetFullscreenState` call. The menu's fullscreen mode is not DXGI exclusive fullscreen.
- `WM_ACTIVATE` restores the desktop mode/minimizes after focus loss when a resolution change was active, and reapplies on return. This can account for brief mode-change blanking, separately from an idle timeout. No new focus or display-power telemetry was found.
- `sdk/runtime/core/input.cpp` initializes SDL gamepad support, not SDL video. Although the dependency's Windows video backend has display-awake and screensaver handling, that does not protect this independently created native game window through gamepad initialization alone.
- No native gameplay handling was found for idle `SC_MONITORPOWER`/`SC_SCREENSAVE` commands.

Windows documentation says display output/CPU work alone do not reset the input idle timer. `ES_DISPLAY_REQUIRED` requests display wakefulness; `ES_SYSTEM_REQUIRED` is a different request. Fullscreen presentation is not a substitute. See [SetThreadExecutionState](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate) and [system sleep criteria](https://learn.microsoft.com/en-us/windows/win32/power/system-sleep-criteria). The API does not suppress screensavers.

## Laptop inspection and limits

Read-only `powercfg /query SCHEME_CURRENT SUB_VIDEO VIDEOIDLE`: current Stride High Performance plan has AC display-off timeout zero (never), battery timeout 600 seconds (ten minutes). `GetSystemPowerStatus` reported plugged in. Screensaver inactive, timeout zero. These are this laptop's settings, not Frost's. The earlier plugged-in soak did not exercise a ten-minute display-off timeout.

`powercfg /requests` failed because administrator privileges are required. No elevation attempted, no active-request measurement claimed. No power plan, timeout, installed-game config or driver changed, and screen-off was not deliberately induced. Thus there is a confirmed missing native keepawake mechanism and a plausible timing match, but no local causal reproduction of Frost's blackout.

## Fix options for review

Recommended small change: on the long-lived native window thread, request `ES_CONTINUOUS | ES_DISPLAY_REQUIRED` while the game window is active, and release on deactivation, minimization, destruction and process handover. Consider `ES_SYSTEM_REQUIRED` only for the same active interval. Check/log failures. Shared runtime change would cover both regions and both graphics backends. Validate focused controller-only play past the configured timeout and normal idle behavior after alt-tab/exit.

An alternative is a scoped `PowerCreateRequest`/`PowerSetRequest(PowerRequestDisplayRequired)` object, with explicit clear/close lifecycle. Optional focused-window screensaver handling should respect secure policies and intentional user sleep. No global power-plan edits or broad exclusive-fullscreen rewrite are needed for the first candidate.

Add activation, resolution-change and display-power notifications to logs alongside the existing device-removal diagnostics. This distinguishes idle power-off, focus/mode switching and genuine graphics reset. Borderless at desktop resolution is a useful mode-switch A/B check, but does not replace a display-awake request.

Evidence to ask Frost for (not posted): actual plugged-in/battery screen-off timeout; controller versus keyboard/mouse input; whether mouse/keyboard immediately restores the display; whether audio/game continue during darkness; time since last keyboard/mouse input; any driver-reset notification/device error in the log. Confirm whether the blackout repeats after another full idle interval or flickers every few seconds: those patterns imply different mechanisms.

## Saved evidence

`D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-power-inspect.py` and `scratch\issue10-power\local-state.json`, `local-display-timeout.txt`, `comment-6692809590.json`. Existing source and extracted mission data inspected read-only. Truck follow-up below uses isolated test folders, not user saves.

## Controlled truck follow-up

Ran isolated EU v0.4.4 / DX11 tests capped at 144 and 60, windowed 640x480, render scale 50%, vsync off, 120 seconds each. Same test entry script: spawn normally, query the truck periodically, then at 25 seconds place only Hulk eight world units to the side and one unit above the truck. No attack/grab inputs, no truck-position edits, and no original scenario, AI or physics changes. This deliberately exercises close-proximity triggers; player placement is a test artifact that may create contact and must be replaced by ordinary gameplay in a stronger reproduction.

At 144 cap the truck root rose after approach: Y 0.552 at 53.373 s, 2.946 at 59.364 s, and 5.048 at 69.296 s. At matching road coordinates, the 60 cap run stayed near zero: X/Z -1531.643/-1601.416, Y 0.150; -1556.350/-1558.439, Y 0.150; -1568.496/-1536.701, Y 0.150. The 144 positions at those stretches have Y 2.946, 4.226 and 5.048. Thus the rise is not explained simply by the common route being elevated. Sample counts: 144 = 21 complete; 60 = 20 complete. Ranges Y 0 to 5.048 versus -0.005 to 0.261. This is a new height-drift lead after proximity, not proof of the exact high-altitude visual symptom or its underlying routine.

Both test processes remained alive to the 120-second stop, no logged errors. Captures introduce stalls, so neither is an uninterrupted stable-FPS test. The 144 capture at approximately 60 seconds shows the disable-truck objective, Hulk on the street, and no clearly visible airborne truck. The 60 capture at approximately 60 seconds shows an Adrenaline Surge tutorial dialog, which pauses progress and explains why sampled mission queries stop. The 144 queries also stop later; do not interpret missing samples as continued measured ascent. No full-mission, damage, pickup or delivery test, no USA or reporter-hardware test. A natural close-approach repeat without capture stalls/tutorial pauses and synchronized actor/smart-prop/body logging is the next step before a targeted physics correction. Existing 60 cap is an A/B option, not a demonstrated complete workaround. A global fixed-timestep rewrite still needs separate approval.

Exact outputs under D:\Projects\Build Cache\Hulk Recompiled\scratch: issue10-close-144.ps1 and issue10-close-60.ps1; isolated issue10-close-144 and issue10-close-60 folders (copied EU exe/DLL, assets junction, settings.toml, mods\Approach Test\args.txt and art\missions\c03m03\c03m03.lua, logs\hulk-3_001.log, result.txt, hdd3 test state and captures). 144 captures: 140221 and 140241; 60 captures: 140528 and 140548. Analysis files: issue10-close-analyze.py, issue10-close-results.json, issue10-close-chart-template.html, issue10-close-comparison.html. The standard chart check helper failed because its internal python3 launcher resolves to the Microsoft Store alias; chart payload created and desktop/mobile rendering inspected through T3 HTML preview without console errors instead. An initial guessed log/image path did not exist and was corrected by listing the run directory. No installed-game files, user saves, physics source, Windows power settings or release artifacts changed.
