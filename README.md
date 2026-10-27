# Hulk Recompiled

A native Windows port and static recompilation of the European original Xbox version of **The Incredible Hulk: Ultimate Destruction**, built on [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp).

> [!IMPORTANT]
> **No game assets or ISO files are included in this repository.**
> Users must provide their own legal retail copy of the European release of *The Incredible Hulk: Ultimate Destruction* (Xbox). Place your game `.iso` (or extracted game files with `default.xbe`) into the `assets/` directory.

---

## Features & Improvements

- **Native PC Mouse Look & Camera Overhaul:**
  - True 1:1 raw mouse sensor input via Windows Raw Input (`WM_INPUT`).
  - Instantaneous camera stop when mouse stops (no floaty lag or artificial deceleration).
  - Continuous dynamic deadzone blending for pixel-precise micro-aiming without abrupt stick jumps.
  - Full sensitivity slider and vertical look inversion (Invert Y).
- **Direct Mouse Hit-Testing & Menu Navigation:**
  - Direct 1:1 hit-testing for all native menus: the mouse pointer is directly on the thing you are clicking.
  - Hovering immediately highlights the item under the pointer with zero lag.
  - Clicking on blank background or margins is safely ignored (no unintended selection).
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
| **Q** | Grab / Pick Up / Throw / Back | B Button |
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
