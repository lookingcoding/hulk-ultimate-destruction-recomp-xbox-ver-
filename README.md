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
- **In-Game PC Settings Overlay (ImGui):**
  - Press **`F1`** or **`~` (Tilde)** at any time to open the settings menu.
  - **Display Modes:** Borderless Fullscreen, Exclusive Fullscreen, Windowed.
  - **Resolution Presets:** Desktop Native, 4K UHD (3840x2160), 1440p QHD (2560x1440), 1080p FHD (1920x1080), 900p, 720p, or custom resolutions.
  - **3D Render Scaling:** 50% to 200% supersampling for crisp, aliasing-free rendering.
  - **VSync & FPS Cap:** VSync toggle and configurable frame rate caps (30, 60, 120, 144, 165, 240, Uncapped).
  - **Anisotropic Filtering:** Off, 2x, 4x, 8x, 16x.
  - Settings persist across launches in `settings.toml`.
- **Native D3D12 & 64-bit Recompilation:**
  - Runs natively on modern 64-bit Windows systems.
  - Vertex buffer synchronization, video playback (Bink), and fiber context switching.

---

## PC Controls

Keyboard and mouse input is mapped to player 1 alongside any connected Xbox/PlayStation controller:

| PC Control | Action | Xbox Controller Equivalent |
| :--- | :--- | :--- |
| **Mouse Move** | Camera Look | Right Analog Stick |
| **Left Click** | Light Attack / Punch | X Button |
| **Right Click** | Guard / Lock-On | Left Trigger (LT) |
| **W / A / S / D** | Movement | Left Analog Stick |
| **Space** | Jump / Charge Jump | A Button |
| **Q** | Grab / Pick Up / Throw | B Button |
| **E** | Special / Critical Mass / Ground Slam | Y Button |
| **Left Shift** | Sprint / Wall Run / Evade | Right Trigger (RT) |
| **Esc** | Pause / Main Menu | Start Button |
| **Tab** | Map / Objectives | Back Button |
| **C** | Reset Camera Behind Hulk | Left Stick Click (L3) |
| **V** | Taunt | Right Stick Click (R3) |
| **1 / 2** | Cycle Target / Smash Camera | White / Black Buttons |
| **F1 or ~** | Toggle PC Graphics & Controls Overlay | *(PC Exclusive)* |

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
