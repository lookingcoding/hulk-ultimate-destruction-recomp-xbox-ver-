#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "common/keyboard.h"
#include "core/input.h"
#include "kernel/memory.h"

namespace hulk { void ReplayPad(int, input::Pad&) {} }
namespace mem { uint8_t* Base() { return nullptr; } }

static bool Check(bool value, const char* message) {
    if (!value) std::fprintf(stderr, "%s\n", message);
    return value;
}

int main() {
    // 1. Test Gameplay Mode (not in menu):
    // - Cursor is hidden
    // - WASD = Move (lx/ly), Left Shift = Sprint (RT), Space = Jump (A), Q = Grab (B), E = Special (Y), Esc = Menu (Start)
    // - Left Click = Punch (X, analog[2]), Right Click = Lock-On (LT, analog[6])
    // - Mouse movement = Camera Look (Right Stick rx/ry)
    hulk::SetMenuModeForTest(-1);
    if (!Check(hulk::IsCursorHiddenForTest(), "Cursor was not hidden in gameplay mode")) return 1;

    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, 'W', 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, VK_SPACE, 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, VK_SHIFT, 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, 'Q', 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, 'E', 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, VK_ESCAPE, 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 100), nullptr);
    hulk::KeyboardMessage(nullptr, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(100, 100), nullptr);
    hulk::KeyboardMessage(nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(200, 200), nullptr);
    hulk::KeyboardMessage(nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(215, 190), nullptr);  // dx = +15 (right), dy = -10 (up)

    input::Pad pad{};
    hulk::KeyboardPad(0, pad);
    if (!Check(pad.connected && pad.ly == 32767 && pad.analog[0] == 255 && pad.analog[1] == 255 &&
               pad.analog[2] == 255 && pad.analog[3] == 255 && pad.analog[6] == 255 && pad.analog[7] == 255 &&
               (pad.buttons & 0x10) != 0 && pad.rx > 0 && pad.ry > 0,
               "Gameplay single-keybind and mouse-look mapping failed")) return 1;

    // Focus loss clears inputs and un-hides cursor
    hulk::KeyboardMessage(nullptr, WM_KILLFOCUS, 0, 0, nullptr);
    if (!Check(!hulk::IsCursorHiddenForTest(), "Cursor remained hidden after focus loss")) return 1;
    pad = {};
    hulk::KeyboardPad(0, pad);
    if (!Check(pad.ly == 0 && pad.analog[0] == 0 && pad.analog[1] == 0 && pad.analog[2] == 0 &&
               pad.analog[7] == 0 && !(pad.buttons & 0x10) && pad.rx == 0 && pad.ry == 0,
               "Held inputs survived focus loss")) return 1;

    // Restore focus and switch to Menu Mode:
    // - Cursor is visible
    // - Left Click = Select (A, analog[0]), Right Click = Back (B, analog[1])
    // - Mouse Wheel & Cursor Move = D-Pad navigation
    hulk::KeyboardMessage(nullptr, WM_SETFOCUS, 0, 0, nullptr);
    hulk::SetMenuModeForTest(1);
    if (!Check(!hulk::IsCursorHiddenForTest(), "Cursor was hidden in menu mode")) return 1;

    hulk::KeyboardMessage(nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 100), nullptr);
    hulk::KeyboardMessage(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(100, 100), nullptr);
    pad = {};
    hulk::KeyboardPad(0, pad);
    if (!Check(pad.analog[0] == 255 && pad.analog[2] == 0, "Menu left click did not map to A (Select)")) return 1;

    hulk::KeyboardMessage(nullptr, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(100, 100), nullptr);
    hulk::KeyboardMessage(nullptr, WM_RBUTTONUP, 0, MAKELPARAM(100, 100), nullptr);
    pad = {};
    hulk::KeyboardPad(0, pad);
    if (!Check(pad.analog[1] == 255, "Menu right click did not map to B (Back)")) return 1;

    hulk::KeyboardMessage(nullptr, WM_KILLFOCUS, 0, 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_SETFOCUS, 0, 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(100, 100), nullptr);
    pad = {};
    hulk::KeyboardPad(0, pad);
    if (!Check((pad.buttons & 0x02) != 0, "Mouse wheel down did not pulse DPAD_DOWN in menu")) return 1;

    hulk::KeyboardMessage(nullptr, WM_KILLFOCUS, 0, 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_SETFOCUS, 0, 0, nullptr);
    hulk::KeyboardMessage(nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(200, 200), nullptr);
    hulk::KeyboardMessage(nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(200, 140), nullptr);  // dy = -60 -> Up
    pad = {};
    hulk::KeyboardPad(0, pad);
    if (!Check((pad.buttons & 0x01) != 0, "Upward cursor movement did not pulse DPAD_UP in menu")) return 1;

    // Verify physical controller state is preserved when idle
    hulk::KeyboardMessage(nullptr, WM_ACTIVATEAPP, 0, 0, nullptr);
    pad = {}; pad.rx = 1234; pad.analog[1] = 200;
    hulk::KeyboardPad(0, pad);
    if (!Check(pad.rx == 1234 && pad.analog[1] == 200 && pad.lx == 0, "Keyboard erased physical controller input")) return 1;

    std::puts("Gameplay mouse-look, cursor hiding, and menu controls passed.");
}
