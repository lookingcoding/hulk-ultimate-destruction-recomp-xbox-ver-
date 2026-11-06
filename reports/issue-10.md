# Issue #10: transport vehicle and display timeouts

Investigation, 2026-11-06. The user subsequently authorized publishing the renderer fix as v0.4.4 and a short issue reply. The transport bug remains open; no vehicle physics were changed.

## Release comparison (seen)

- Remote v0.4.3 resolves to `f56767bef6abb51cf4f7b620a38d96efbea94902`, the initial worktree HEAD. The initial SDK changes were the project's applied patches.
- Local v0.4.3 ZIP SHA256 `DFD8F802370417F6DC2D0CF73494DED3B28B14517FBF0E0B848ABB89E5B3DC69` matches the GitHub release asset digest. Its European executable and the initial build executable both hash to `DEE1DB2761B754C7DA8EFEBF3A57CE04936B1A49136719285E6CE9C985CFDCE3`.
- v0.4.2 is `5a71b98`. Changes since that release include mods, language selection, wheel camera distance, DX11 support, and controller cursor handling. The cursor commit alone is not the full release delta.
- Comparing both packaged compiler folders found differences only in the two host import libraries. The recompiler and CPU headers did not change.
- The frame-rate unlock (`6431c3d`) predates v0.4.2. The DX12 renderer did not change between v0.4.2 and v0.4.3. DX11 is selected explicitly or on DX12 initialization failure; auto still chooses DX12 on this laptop.
- No new vehicle-physics hook or mission-data patch was found in the release delta. A regression caused by higher achieved frame rates or a particular mission state remains a hypothesis, not an established cause.

## Display investigation and local changes

**Seen:** `sdk/runtime/gpu/d3d12.cpp` reset the SRV ring or sampler cache to slot zero on exhaustion, overwriting shader-visible descriptors while recorded/submitted draws could still reference them. Sampler descriptors persist across frames, so this can depend on previously encountered materials rather than just one busy frame.

`patches/zzzzz-d3d12-descriptor-lifetime.patch` now allocates a replacement heap and retains the old heap in the current frame until its GPU fence completes. It invalidates both cached descriptor-table bindings and allocates samplers before binding either table. Both synchronous submission and normal frame recycling release retired heaps only after completion.

`XBE_D3D12_DESCRIPTOR_STRESS=1` reduces heap capacities to exercise both rollover paths in short runs. It is absent in ordinary play. This is a real renderer/GPU test, not a mocked allocator test.

DX12 now checks command-list close, queue signal, fence event/completion, and Present failures. DX11 now checks Present failures, carried in the existing `zzzz-d3d11-renderer.patch`. Failures log the HRESULT and device-removed reason and stop instead of silently continuing with an invalid device. This is **not automatic device recreation**. The device-loss failure paths have not been induced on this laptop.

**Inferred:** the descriptor defect is a plausible GPU instability cause. The reporter's repeating blackout has not been reproduced, and their GPU, renderer, settings, and log are unknown. Fixing this defect does not establish that it caused their report.

Microsoft's [descriptor overview](https://learn.microsoft.com/en-us/windows/win32/direct3d12/descriptors-overview) documents the lifetime requirement; its [TDR documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery) describes display resets. No driver settings, registry timeout values, or installed drivers were changed.

## Transport investigation

- The disc's string table calls `c03m02` Authority Issues and `c03m03` Frantic Recovery. The latter's `art/missions/c03m03` data was inspected.
- The original mission enables `am_SetOnlyTakeDamageFromThrown(DivisionTruck, true)` during the chase and disables it later. Resistance to ordinary attacks during that stage is intentional; failure to take damage from thrown objects remains unverified.
- The game's existing `-level c03m03` option is handled at EU `0x8F920` (option match at `0x8F960`). Test-only mods overlay `args.txt` and the mission entry script to start the mission and request truck-position queries once per second. Mission scenario, truck AI, route, collision, and damage data are unchanged.
- `src/game/vehicle_probe.cpp` optionally logs the result of EU `am_GetActorPosition` (`0x26C30`, thiscall with hidden vector output at esp+4 and name at esp+8). It calls the original first and does not change actor state. Enabled only by `HULK_TEST_VEHICLES`; no USA probe is provided. An early positioning experiment used a wrong sign for the player's z coordinate and is not counted as a clean mission test.
- The original save under `release/hdd` and the Lightning Strike profile under `release/hdd3` were read only. Direct mission tests use their own folders/profiles; no save modification was required.
- The original save's SHA1 is still `89C3AF3A6B854BA8386C8E54AB31D5D467FD8B83`, matching the earlier vault entry's recorded prefix.

## Validation and remaining work

Laptop: Intel UHD Graphics, driver `32.0.101.6129`, Windows 11 Education 24H2, i3-1315U, 16 GB RAM. EU disc only.

- Both regional executable/DLL builds pass. All four CTest cases pass.
- Initial 195-second DX12 heap stress run: normal timed end, zero logged runtime errors, both heap rollover types exercised, gameplay captures inspected. No System event 4101 during the run.
- Final-build Frantic Recovery DX12 heap-stress run: 195 seconds, normal timed end, zero logged runtime errors, 90 complete truck-position samples, y 0.003 to 0.255. Reduced heap capacities exercised SRV rollover; the earlier tutorial stress run explicitly logged both heap types. Captures at approximately 60 and 120 seconds render correctly. Actual median frame rate was 51.2 with a 60 cap; this was not a performance benchmark.
- Clean DX11 chase runs at caps 60 and 120: 150 seconds each, normal timed ends and zero logged runtime errors. Measured median frame rates were 60 and 120 respectively. Complete truck-position samples: 76 at 60 fps (y 0.003 to 0.246), 77 at 120 fps (y 0 to 0.267). No flying truck was reproduced. The player was stationary at the original mission spawn; the scripts only queried the truck's position.
- Long DX12 tutorial soak: 1,200 seconds, normal timed end, zero logged runtime errors and zero System display-reset events (4101). The run includes boot/intro time and mostly stationary tutorial gameplay, not 20 minutes of active mission combat. This run uses the lifetime fix from before the final device-error diagnostics changes; the final build is covered by the city stress run above. Some test processes overlapped, so these runs do not establish comparative performance.
- Both modified renderer patches apply to clean SDK source and reproduce the working source (DX11 compared with line endings normalized). Reverse checks also pass against the patched worktree.
- Manual Windows automation failed repeatedly with `Computer Use native pipe is unavailable: failed to connect native pipe: The system cannot find the file specified. (os error 2)`. Application test input and native renderer captures remain usable.
- Not established: the transport's failure mechanism, successful shield damage/grab/delivery, whole mission completion, the reporter's exact blackout cause, GPU reset recovery, USA runtime behavior, other GPUs/drivers, or full campaign stability. No gameplay workaround is represented as a confirmed transport fix.

Artifacts and reproducible local scripts are under `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10*`. `issue10-run.ps1` drives the tutorial soak/stress runs; `issue10-vehicle-run.ps1` drives direct mission runs with `-Cap`, `-Renderer`, and `-Seconds`. `issue10-lua.py` reads the disc's float-number Lua chunks for static inspection. `issue10-verify.py` checks clean patch application and summarizes telemetry. `issue10-artifacts.json` inventories exact output paths without traversing the assets junctions. `issue10-vehicle-results.json` contains the measured vehicle samples' summary. The published release archive and user-facing standalone installation are unchanged; the development build in the build cache has been updated.
