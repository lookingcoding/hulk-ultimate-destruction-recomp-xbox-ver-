# Hulk Recompiled - work in progress

This is a native Windows port project for the original Xbox version of The Incredible Hulk: Ultimate Destruction. It is an experimental playable build validated through the opening junkyard tutorial, not yet a verified full-campaign port.

Verified input:

- Disc: your own retail disc image of the game (`.iso`, any file name) in `assets/`.
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
- `patches/zz-file-view.patch`: adds `GameInfo::file`, through which the game code can change what the game reads of a file without touching it on disk. `src/common/display_menu.cpp` uses it to serve `misc.rcf` with edited `frontend\fe_real.txt` and `frontend\fe_run.txt` (a Display Options row in the main and pause Options screens, opening a screen of text selectors) and string tables, all placed past the archive's end with the directory pointing at them. The selectors' callbacks are registered beside the game's own (`0x000C4500`) and share the PS2/GameCube-only `AudioQualityChanged` handler (`0x000BBEC0`), told apart by their owners; an `OnHighlight` function sharing the unused `Print` handler (`0x000B46B0`) sets each selector to the current setting as the game does for its own screens (`src/game/hooks.cpp`). Verified in game: both screens render with the game's own art, show the current settings, and changes apply and save to `settings.toml`.
- Host-only changes relink the game DLL, preventing an unnecessary full first-launch rebuild caused by the SDK's executable/DLL freshness check.
- `config/game.toml` and `config/game_usa.toml` add the two resume points of the game's guarded-call wrapper: `0x32DC48` after the frame install at `0x32DC20` and `0x2B9872` after the one at `0x2B9850` (USA: `0x215318` after `0x2152F0`, `0x2D6F32` after `0x2D6F10`). A guest exception inside such a call is handled by restoring the frame's `esp` and jumping to the saved return address (`0x3435B0`, USA `0x3471D4`); without an entry there the emulator trapped with "indirect call to 0032DC48 has no recompiled function" during a later mission (`logs/hulk_019.log`).
- Keyboard input maps to controller port 1 and clears held keys on focus loss (`tests/keyboard.cpp`). Optional controller replay is enabled only through `HULK_TEST_INPUT` for smoke tests (`src/common/replay.cpp`, `tools/Send-TestInput.ps1`).
- Button prompts cover the whole string table: every one of the 27 `\b` codes in `frontend\lang\str_*.txt` is either a mapped button (`ju us at sp ta ru la lr ra se jc uc ac sc` → key or mouse wording, `src/common/display_menu.cpp`) or a map/objective icon that stays an icon (`mt mn mj mh mp mi mg me ms md of ob os`). The title screen's own lines are now worded for the keyboard ("Press Enter", "Press any key", "[Space] Start") instead of "Press START", and `settings.toml`'s `button_prompts` (`auto`, `keyboard`, `controller`) picks whose tooltips the tables carry when a controller and the keyboard are both in use.

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
- Unit and regression tests (`hulk_analysis_tests`, `hulk_input_tests`, `hulk_kernel_io_tests`, `hulk_display_menu_tests`) all pass via `ctest`.
- All five SDK patches (`audio-prebuffer.patch`, `optical-volume.patch`, `vtable-discovery.patch`, `z-kernel-io.patch`, `zz-file-view.patch`) cleanly apply to and reverse-check against the pinned SDK submodule commit.

Known limits and remaining validation:

- Saving is disabled by the game during the opening tutorial mission; full campaign save/load cycles after completing the opening mission remain to be validated in gameplay.
- The symbol summary still lists 50 unresolved/absent library symbols (including DirectSound exports running their original Xbox code).
- Audio starts prebuffered, but occasional starvation/queue growth (up to 100 ms) still occurs under load; listening quality remains unverified.
- Physical controllers, physical keyboard window play, later campaign missions, and full-game completion remain to be validated by user playtesting.
