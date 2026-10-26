# Hulk Recompiled - work in progress

This is a native Windows port project for the European original Xbox release of The Incredible Hulk: Ultimate Destruction. It is an experimental playable build validated through the opening junkyard tutorial, not yet a verified full-campaign port.

Verified input:

- Disc: `D:\Incredible Hulk, The - Ultimate Destruction (Europe) (En,Fr,Es,It)\Incredible Hulk, The - Ultimate Destruction (Europe) (En,Fr,Es,It).xiso.iso`
- Disc size: 2,934,767,616 bytes; XDVDFS game partition at offset 0.
- Title ID: `56550039`; retail entry point: `0020AAC6`.
- `default.xbe`: 5,210,112 bytes; SHA256 `CE236582A5F65F0732B0EFC612AC83430B2A98276C6FF8763979ABC0E94782F4`.
- XDK 5849: XAPILIB, XBOXKRNL, LIBCMT, D3D8, XGRAPHC, DSOUND.
- SDK: TGP482/XboxXBE-Recomp, base commit `0fd00442660573dc342b41c11c6574318f0e8698`, pinned as `sdk/` submodule with the local patches under `patches/` applied by the build script.
- LLVM 23.1.4 portable compiler archive verified against GitHub's SHA256 digest.
- All 26 disc files extracted; streamed data copies SHA256 verified and the complete path/size manifest checked.

Paths:

- Project and local game data: `D:\Projects\Active\Hulk Recompiled`.
- Build, dependency downloads, logs, generated C++: `D:\Projects\Build Cache\Hulk Recompiled`.
- Standalone local install: `D:\Games\Standalone\Hulk Recompiled`.
- Portable compiler: `D:\Projects\Build Cache\Tools\clang+llvm-23.1.4-x86_64-pc-windows-msvc`.
- `generated/` is a junction to the build cache, and the build's `assets/` is a junction to the project's assets.

Static code generation and native executable/DLL compilation succeeded. The final generator reports 46,341 discovered function entries and also reports unsupported instruction decodes; some discovered entries may be data misidentified as code. Successful compilation does not demonstrate that every executable path has been recovered correctly.

The two missing Bink video helpers (`BinkWait` at `0x00367400`, `BinkNextFrame` at `0x00368C90`) and the XDK 5849 dynamic vertex-buffer synchronization helper (`D3D_BlockOnNonSurfaceResource` at `0x0038B130`) were traced and mapped; see reports/symbol-notes.md. The missed thread-notification callback at `0x0034F465` was added to the discovered entry points.

Runtime and SDK fixes:

- `src/game/fibers.cpp`: Xbox SwitchToFiber at `0x00350EBD` needs to switch the native C++ continuation as well as guest ESP. It now bridges to Windows fibers, reproduces the guest nonvolatile-register/SEH save frame, and releases native stacks when the guest deletes a fiber.
- `patches/optical-volume.patch`: CdRom0 retains optical-drive metadata with extracted data too. Volume queries return 2048-byte DVD sectors and zero writable/free space, while HDD sectors remain 512 bytes. The game checks DVD sector size at `0x00088411`; that check remains in place.
- `patches/vtable-discovery.patch`: read-only virtual-method pointers after short/near tail jumps are established before speculative decoding. The first punch exposed a missing method at `0x0001D570`; the patched generator now includes it. See reports/vtable-notes.md.
- `config/game.toml` (`D3D_BlockOnNonSurfaceResource = "0x0038B130"`): maps XDK 5849's vertex-buffer wait helper so dynamic vertex buffers are not overwritten before the D3D12 renderer consumes them, resolving overlapping menu text.
- `patches/audio-prebuffer.patch`: prebuffers 30 ms of audio frames before starting the XAudio2 source voice and raises the audio worker thread priority.
- `patches/z-kernel-io.patch`: implements the six Xbox kernel exports imported by this title that were absent from the pinned SDK (`NtCreateIoCompletion`, `NtSetIoCompletion`, `NtRemoveIoCompletion`, `ObOpenObjectByName`, `NtUserIoApcDispatcher`, `IoQueryVolumeInformation`). Boot now reports `boot: kernel imports resolved, 0 missing`. See reports/kernel-io-notes.md.
- Host-only changes relink the game DLL, preventing an unnecessary full first-launch rebuild caused by the SDK's executable/DLL freshness check.
- Keyboard input maps to controller port 1 and clears held keys on focus loss (`tests/keyboard.cpp`). Optional controller replay is enabled only through `HULK_TEST_INPUT` for smoke tests (`src/common/replay.cpp`, `tools/Send-TestInput.ps1`).

Verified runtime milestones:

- The original 512-byte DVD report produced the game's original-disc error screen; the corrected report passes that check.
- Intro video rendering was captured through the SDK's renderer (`release/captures/115943/final.bmp`, Dolby logo).
- The native title screen was captured after the scripted Start inputs (`release/captures/120555/final.bmp`).
- After mapping `D3D_BlockOnNonSurfaceResource`, difficulty selection (`release/captures/131227/final.bmp`) and pause menu (`release/captures/131446/final.bmp`) text render cleanly without overlap.
- In the 600-second interactive tutorial progression (`release/logs/hulk-2_004.log`), the game booted with zero missing kernel imports, initialized `hdd2\E\UDATA\56550039` metadata (`SaveImage.xbx`, `TitleImage.xbx`, `TitleMeta.xbx`), held ~30 fps in gameplay, and advanced through the tutorial prompts with zero logged runtime errors:
  - Walking (`release/captures/131514/final.bmp`: "Walk Hulk around the junkyard with L")
  - Running (`release/captures/131537/final.bmp`: "Run around by walking with L and pulling and holding RT")
  - Approaching an object (`release/captures/131611/final.bmp`: "Hulk can pick up large objects with ease. Walk close to an object and press B to pick it up")
  - Picking up the object (`release/captures/131643/final.bmp`: "Throw a held object by pressing the B button")
  - Throwing the object and advancing to jumping (`release/captures/131707/final.bmp`: "Press A to jump")
- The 195-second automated smoke test (`tools/Test-Game.ps1`, `combat-test.log`, `release/logs/hulk-2_002.log`) also exercised jump and punch inputs without logged runtime errors.
- Unit and regression tests (`hulk_analysis_tests`, `hulk_input_tests`, `hulk_kernel_io_tests`) all pass via `ctest`.
- All four SDK patches (`audio-prebuffer.patch`, `optical-volume.patch`, `vtable-discovery.patch`, `z-kernel-io.patch`) cleanly apply to and reverse-check against the pinned SDK submodule commit.

Known limits and remaining validation:

- Saving is disabled by the game during the opening tutorial mission; full campaign save/load cycles after completing the opening mission remain to be validated in gameplay.
- The symbol summary still lists 50 unresolved/absent library symbols (including DirectSound exports running their original Xbox code).
- Audio starts prebuffered, but occasional starvation/queue growth (up to 100 ms) still occurs under load; listening quality remains unverified.
- Physical controllers, physical keyboard window play, later campaign missions, and full-game completion remain to be validated by user playtesting.
