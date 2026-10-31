# Hulk Recompiled

A native Windows port and static recompilation of the original Xbox version of **The Incredible Hulk: Ultimate Destruction**, built on [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp).

> [!IMPORTANT]
> **No game assets or ISO files are included in this repository.**
> Users must provide their own legal retail copy of *The Incredible Hulk: Ultimate Destruction* (Xbox). Place your game `.iso` (any file name) or the extracted game files with `default.xbe` into the `assets/` directory.

## Download & Play

1. Download `HulkRecompiled-<version>-windows-x64.zip` from [Releases](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-/releases) and extract it.
2. Put your Xbox disc image (`.iso`) in the `assets` folder.
3. Run `Hulk Recompiled.exe`. It loads your disc and starts the game; the game itself, its settings and saves are in the `game` folder.

The game checks your disc image when it starts and tells you if it can't use it. The first launch builds the game's code from your disc with the included `compiler` folder, which takes a few minutes and needs about 1 GB of free space; later launches start straight away.

---

## Features & Improvements

- **Native PC Mouse Look & Camera Overhaul:**
  - True 1:1 raw mouse sensor input via Windows Raw Input (`WM_INPUT`).
  - Instantaneous camera stop when mouse stops (no floaty lag or artificial deceleration).
  - Continuous dynamic deadzone blending for pixel-precise micro-aiming without abrupt stick jumps.
  - Full sensitivity slider and vertical look inversion (Invert Y).
- **Direct Mouse Hit-Testing & Menu Navigation:**
  - Hovering a menu row or tab highlights it and clicking selects it. The port reads the game's real highlighted row and steps the D-pad until it matches the one under the pointer, so greyed-out rows, wrapping and every list length work.
  - Clicking a tab (Options / Story Mode / Extras) from inside a list goes back and opens that tab. Clicking or scrolling on a selector changes its value.
  - Clicking off every row does nothing. In dialogs the left click is the A button.
- **Button prompts follow your input device:** with no controller plugged in, tooltips name your keys and mouse buttons (e.g. "Walk Hulk around with [WASD]", "press [Q] to pick it up"); with a controller they show its buttons. The game reads its strings as it loads, so plugging in or unplugging a controller mid-game shows from the next load. The menus' bottom prompt ("[Enter] Select  [Q] Back", Delete on save screens) changes at once, and the title screen asks for Enter instead of START. `settings.toml`'s `button_prompts` picks the tooltips when both are in use: `"auto"` (a controller's while one is plugged in, else the keyboard's), `"keyboard"` or `"controller"`.
- **Camera not inverted by default:** Invert X-Axis and Invert Y-Axis start at No, and No now means the camera follows the stick (the original game labelled it the other way round). Your choice is kept in `settings.toml`, the same for every save.
- **Display Options in the game's own Options menu:**
  - Main menu > Options and the pause menu's Options each have a **Display Options** row, built from the game's own frontend script, art, fonts and transitions, and driven by the controller or keyboard like every other row. Its strings are added in English, French, Spanish and Italian.
  - **Display Mode:** Fullscreen, Borderless, Windowed.
  - **Resolution:** Desktop, 1280x720, 1600x900, 1920x1080, 2560x1440, 3840x2160 (a size set in `settings.toml` shows as Custom).
  - **Frame Rate Limit:** 30, 60, 120, 144, monitor refresh rate, unlimited.
  - **V-Sync** and **Render Scale** (50% to 200%).
  - Changes apply once the selection settles and persist in `settings.toml`. The game's files are not modified: the runtime serves edited `frontend\fe_real.txt` (main menu), `frontend\fe_run.txt` (pause menu) and string tables through a view of `misc.rcf` (see `src/common/display_menu.cpp`).
- **Native D3D12 & 64-bit Recompilation:**
  - Runs natively on modern 64-bit Windows systems.
  - Vertex buffer synchronization, video playback (Bink), and fiber context switching.

---

## PC Controls

Keyboard and mouse input is mapped to player 1 alongside any connected Xbox/PlayStation controller:

| PC Control | Action | Xbox Controller Equivalent |
| :--- | :--- | :--- |
| **Mouse Move** | Camera Look (in gameplay) / Menu Aim | Right Analog Stick / Direct Hover |
| **Left Click** | Light Attack / Punch (gameplay) / Select (menus) | X Button / A Button |
| **Right Click** | Guard / Lock-On (gameplay) / Back (menus) | Left Trigger (LT) / B Button |
| **Mouse Wheel** | Menu Item Scroll | D-Pad Up / Down |
| **W / A / S / D** | Movement | Left Analog Stick |
| **Space** | Jump / Charge Jump / Select | A Button |
| **Enter** | Select (menus) | A Button |
| **Q** | Grab / Pick Up / Throw / Back | B Button |
| **Delete** | Delete (save screens) | X Button |
| **E** | Special / Critical Mass / Ground Slam | Y Button |
| **Left Shift** | Sprint / Wall Run / Evade | Right Trigger (RT) |
| **Esc** | Pause / Main Menu / Back | Start Button / Back |
| **Tab** | Map / Objectives | Back Button |
| **C** | Reset Camera Behind Hulk | Left Stick Click (L3) |
| **V** | Taunt | Right Stick Click (R3) |
| **1 / 2** | Cycle Target / Smash Camera | White / Black Buttons |

---

## Building from Source

### Prerequisites
- Windows 10/11 64-bit.
- Visual Studio 2022 C++ Build Tools (with CMake and Ninja).
- Clang/LLVM toolchain.
- Retail disc image (.iso) or extracted game files.

### Steps
1. Clone the repository with submodules:
   ```cmd
   git clone --recurse-submodules https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-.git
   cd hulk-ultimate-destruction-recomp-xbox-ver-
   ```
2. Place your disc image or `default.xbe` in the `assets/` folder.
3. Run the automated build script:
   ```cmd
   build.bat
   ```
4. Launch the game using:
   ```cmd
   play.bat
   ```

---

## License & Credits

- Xbox XBE Recomp by TGP482 (MIT); third-party credits in `sdk/THIRD_PARTY.txt`.
- Kernel export structures follow nxdk (CC0).
- This project is an independent recompilation effort. The Incredible Hulk: Ultimate Destruction is property of its respective copyright holders.
