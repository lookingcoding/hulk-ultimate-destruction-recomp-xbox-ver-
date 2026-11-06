# Issue #10: 144 fps follow-up

Frost subsequently supplied a v0.4.4 airborne-truck screenshot. See [issue-10-photo.md](issue-10-photo.md) for the observed symptom and the interaction/phase gaps in these stationary-player tests. These results do not contradict his photo.

Local investigation on 2026-11-06, after frost4d367 reported playing at a stable 144 fps. No physics change, commit, push, release, or GitHub reply was made during this follow-up.

## Observed result

The flying truck was **not reproduced in the initial chase** at the 144 fps target. The lower-render-load run achieved sustained intervals near 144 fps, but neither entire four-minute run was perfectly locked at 144. This does not rule out a failure requiring uninterrupted 144 fps, another mission phase, different hardware, or a different disc.

Both tests used the v0.4.4 European executable (SHA256 `E6734D50B3C196FB6F9A60120339BF56B27706DC0E2D6C441F8C20F24A0879CC`), Direct3D 11, 640x480 window, vsync off, the original `-level c03m03` launch option, and the previously validated opt-in actor-position query. The mission scenario, truck AI, route, collision, physics, and player spawn were unchanged. Test-only scripts queried the truck once per second. The player remained stationary at the original mission spawn.

- **First run, 240 seconds:** 100% render scale and two full renderer captures. After 30 seconds, median 133.1 fps; longest continuous interval of per-second averages within 142-146 fps was 16.06 seconds. Captures caused substantial stalls, so this is not a stable-144 test. There were 115 complete truck-position samples, Y 0.001-0.260; no logged runtime errors and no early process exit.
- **Second run, 240 seconds:** 50% render scale, all renderer captures disabled. After 30 seconds, median 144.0 fps; 188 of 206 per-second averages were within 142-146 fps. The longest such continuous interval was 55.20 seconds (30.202-84.405 seconds after launch). Minimum/maximum after 30 seconds were 110.9/145.0 fps; occasional drops remain part of the result. There were 127 complete truck-position samples, Y 0-0.280, settling near Y 0.2 while moving along the road; no logged runtime errors and no early exit.

These are per-second frame-rate averages, not a per-frame latency trace. The 142-146 interval is an explicitly chosen tolerance for reporting sustained near-144 operation, not proof of a mathematically constant frame rate. A one-second position query could miss a brief event between samples. The low-render-load run has telemetry rather than a continuous video recording.

Shield damage/breaking, throwing objects at the shield, grabbing/carrying/dropping the truck, delivery, and mission completion were **not exercised**. Neither run proves the absence of a bug in those phases. USA runtime and the reporter's renderer/GPU/settings remain untested or unknown. The investigation therefore does not justify saying that all vehicle physics are safe at 144 fps.

## Timing code inspected

**Seen in the original European XBE:**

- `0x86430` reads the current clock, subtracts the previous clock value, clamps the elapsed value to the clock object's configured maximum, and accumulates elapsed time. In particular, `0x8644E-0x8646F` computes the measured delta and `0x864B8-0x864CF` bounds it.
- RTTI identifies `PhysicsObject@physics` (type descriptor `0x4C822C`, vtable `0x42B3AC`). Its update at `0x238E00` rejects non-positive time arguments, then passes the same argument to vtable slot `+0x98`, which resolves to `0x238F90` (`0x238F26-0x238F2B`).
- `0x238F90` uses its time argument in damping: `0x238F93-0x238FA2` computes `1 - time * field_164`, with bounds against 0 and 1. Later operations at `0x23912E-0x239168` and `0x2391CF-0x239214` multiply motion/force-related state by that time argument. This is time-scaled integration, rather than a constant displacement per rendered frame in this routine.
- The occupation/patrol agent update at `0x103E50` accepts timing arguments and passes them through its base/controller updates, including `0x10403F-0x104055`.

**Interpretation, not a complete proof:** the engine paths inspected do use elapsed time. A blanket claim that all physics simply assume 30 updates per second is unsupported. Conversely, time-scaled integration can still have timestep-sensitive constraints, damping, collision behavior, or gameplay code elsewhere; this inspection does not prove frame-rate independence of the truck's full lifecycle. The existing frame-unlock hook comment saying no logic needs the old pace is broader than what has been established by testing.

## Options if the reporter confirms a high-frame-rate failure

1. **A/B test with the existing frame-rate setting at 60, then the original 30 fps if necessary.** This changes no physics implementation and is the simplest temporary workaround/test. It also lowers rendering frame rate. The earlier clean 60/120 chase tests and these 144 runs do not establish a universal safe cap for later mission phases.
2. **Targeted vehicle fix after identifying the failing phase/routine.** Correct a demonstrated per-frame force, constraint, or timing assumption locally. No such defect has been identified yet; do not invent an arbitrary multiplier or run the truck's update less often without preserving elapsed time and collision ordering.
3. **Separate simulation timing from rendering.** An accumulator driving a fixed 30/60 Hz simulation, with rendering/interpolation at 144 Hz, is a larger architectural option. It must account for collision processing, actors/AI, script timers, animation, and held objects together. Simply replacing a 1/144-second delta with 1/60 while still updating 144 times per second would speed up simulated time; that is not a correct fixed-timestep implementation. User approval is required before this larger change.

Recommendation: retain current behavior while awaiting frost4d367's v0.4.4 result and obtain the exact failing phase/video/save if it persists. A 144-specific cause remains possible but is not demonstrated here.

## Local evidence

- `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-vehicle-144-d3d11\`: first run, settings, mods, log, captures, result, and `observations.json`.
- `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-144-no-capture\`: lower-render-load run, settings, mods, log, result, and `observations.json`.
- `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-144-stable.ps1`: repeatable lower-render-load runner; existing `issue10-vehicle-run.ps1` drove the first run.
- `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-144-analyze.py` and `issue10-144-results.json`: analysis code and exact summary, including stalls.
- `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-144-static.py` and `issue10-144-static\*.asm`: static inspection and disassembly slices.
- `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-144-chart-template.html`, `issue10-144-chart.py`, and `issue10-144-evidence.html`: plot source and displayed evidence chart. Matplotlib was unavailable; no dependency was installed. An initial chart read used the Windows default text encoding; the chart generator/read-back were corrected to UTF-8 and previewed successfully.
