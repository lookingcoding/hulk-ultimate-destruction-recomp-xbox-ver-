#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "common/addresses.h"
#include "common/keyboard.h"
#include "core/input.h"
#include "kernel/memory.h"

namespace hulk { void ReplayPad(int, input::Pad&) {} }
static uint8_t* guest_base = nullptr;
namespace mem { uint8_t* Base() { return guest_base; } }

static bool Check(bool value, const char* message) {
    if (!value) std::fprintf(stderr, "%s\n", message);
    return value;
}

static bool TestEnterMenus(const char* slot_name) {
    // Exercise the real highlighted-widget lookup with either disc's frontend addresses.
    std::vector<uint8_t> guest(hulk::addr::kFrontend + sizeof(uint32_t));
    guest_base = guest.data();
    const auto write = [&](uint32_t address, auto value) {
        std::memcpy(guest.data() + address, &value, sizeof(value));
    };
    // Real widgets store their separately allocated label pointer at +0x14.
    constexpr uint32_t manager = 0x10000, screen = 0x20000, selector = 0x21000, text = 0x31000;
    uint64_t hash = 0;
    for (const char* p = slot_name; *p; ++p) hash = (hash * 0x1003Full) ^ uint64_t(*p);
    write(hulk::addr::kFrontend, manager);
    write(manager + 0x175C, screen);
    write(screen + 0x48, text);
    write(screen + 0x44C, selector);
    write(selector, hulk::addr::kTextSelectorVtable);
    write(selector + 0x14, text);
    write(text + 0x20, hash);
    const auto reset = [] {
        hulk::KeyboardMessage(nullptr, WM_KILLFOCUS, 0, 0, nullptr);
        hulk::KeyboardMessage(nullptr, WM_SETFOCUS, 0, 0, nullptr);
        hulk::SetMenuModeForTest(1);
        hulk::SetGamepadDetected(false);
    };
    const auto key = [](uint32_t message, intptr_t flags = 0) {
        hulk::KeyboardMessage(nullptr, message, VK_RETURN, flags, nullptr);
    };
    const auto poll = [] {
        input::Pad pad{};
        hulk::KeyboardPad(0, pad);
        return pad;
    };

    reset();
    key(WM_KEYDOWN);
    auto pad = poll();
    if (!Check(pad.buttons == 0x08 && pad.analog[0] == 0, "Enter did not advance a selector without also selecting")) return false;
    Sleep(35);  // finish the pulse, then observe its release gap
    poll();
    key(WM_KEYDOWN, intptr_t(1) << 30);  // Windows held-key repeat
    pad = poll();
    if (!Check(pad.buttons == 0 && pad.analog[0] == 0, "Held Enter repeated the selector change")) return false;
    Sleep(25);
    if (!Check(poll().buttons == 0, "Held Enter queued another selector change")) return false;
    key(WM_KEYUP);
    key(WM_KEYDOWN);
    if (!Check(poll().buttons == 0x08, "A second Enter press did not advance the selector")) return false;

    reset();
    key(WM_KEYDOWN);
    key(WM_KEYUP);  // a tap shorter than one input poll must still register
    pad = poll();
    if (!Check(pad.buttons == 0x08 && pad.analog[0] == 0, "A quick Enter tap was lost")) return false;

    reset();
    key(WM_KEYDOWN);
    reset();
    if (!Check(poll().buttons == 0, "Focus loss left a queued Enter change")) return false;

    reset();
    hulk::SetGamepadDetected(true);
    key(WM_KEYDOWN);
    pad = {}; pad.connected = true; pad.buttons = 0x04; pad.analog[0] = 123;
    hulk::KeyboardPad(0, pad);
    if (!Check(pad.buttons == 0x0C && pad.analog[0] == 123, "Enter failed alongside a physical controller")) return false;

    reset();
    // A closer adjacent widget must not be mistaken for the highlighted row's owner.
    write(screen + 0x450, text - 0x100);
    write(text - 0x100, hulk::addr::kTextSelectorVtable);
    write(selector, uint32_t(0));  // ordinary action row
    key(WM_KEYDOWN);
    pad = poll();
    if (!Check(pad.analog[0] == 255 && pad.buttons == 0, "Enter no longer selects ordinary menu rows")) return false;

    reset();
    write(selector, hulk::addr::kTextSelectorVtable);
    write(manager + 0x1760, uint32_t(0x30000));  // confirmation dialog overlays the selector
    key(WM_KEYDOWN);
    pad = poll();
    if (!Check(pad.analog[0] == 255 && pad.buttons == 0, "Enter did not confirm a dialog")) return false;

    reset();
    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, VK_RIGHT, 0, nullptr);
    if (!Check(poll().buttons == 0x08, "Right-arrow option navigation changed")) return false;
    reset();
    hulk::KeyboardMessage(nullptr, WM_KEYDOWN, VK_SPACE, 0, nullptr);
    pad = poll();
    if (!Check(pad.analog[0] == 255 && pad.buttons == 0, "Space's Select binding changed")) return false;

    reset();
    hulk::SetMenuModeForTest(-1);
    key(WM_KEYDOWN);
    pad = poll();
    if (!Check(pad.analog[0] == 255 && pad.buttons == 0, "Enter's gameplay jump binding changed")) return false;

    guest_base = nullptr;
    reset();
    return true;
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

        // 3. Test Menu Mode with Gamepad Detected:
    // - Cursor is hidden in menu mode when a physical controller is detected
    // - Mouse inputs in menu do not interfere with controller navigation
    hulk::KeyboardMessage(nullptr, WM_SETFOCUS, 0, 0, nullptr);
    hulk::SetMenuModeForTest(1);
    pad = {}; pad.connected = true;
    hulk::KeyboardPad(0, pad);
    if (!Check(hulk::IsGamepadDetected(), "Gamepad was not detected after connected pad poll")) return 1;
    if (!Check(hulk::IsCursorHiddenForTest(), "Cursor was not hidden in menu mode with gamepad detected")) return 1;

    // Mouse movement/clicks while in menu with gamepad should be ignored
    hulk::KeyboardMessage(nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(200, 200), nullptr);
    hulk::KeyboardMessage(nullptr, WM_MOUSEMOVE, 0, MAKELPARAM(200, 140), nullptr);
    hulk::KeyboardMessage(nullptr, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 100), nullptr);
    hulk::KeyboardMessage(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(100, 100), nullptr);
    pad = {}; pad.connected = true;
    hulk::KeyboardPad(0, pad);
    if (!Check(pad.buttons == 0 && pad.analog[0] == 0, "Mouse menu navigation was not ignored while gamepad detected")) return 1;

    // Disconnecting the controller in menu mode restores cursor visibility for mouse/keyboard
    pad = {}; pad.connected = false;
    hulk::KeyboardPad(0, pad);
    if (!Check(!hulk::IsGamepadDetected(), "Gamepad remained detected after disconnected pad poll")) return 1;
    if (!Check(!hulk::IsCursorHiddenForTest(), "Cursor remained hidden in menu mode after gamepad disconnected")) return 1;

    // In gameplay mode, cursor is always hidden whether gamepad is connected or not
    hulk::SetMenuModeForTest(-1);
    if (!Check(hulk::IsCursorHiddenForTest(), "Cursor was not hidden in gameplay mode without gamepad")) return 1;
    hulk::SetGamepadDetected(true);
    if (!Check(hulk::IsCursorHiddenForTest(), "Cursor was not hidden in gameplay mode with gamepad")) return 1;
    hulk::SetGamepadDetected(false);

    if (!TestEnterMenus("FEsubMenu_text1") || !TestEnterMenus("FEpause_text1")) return 1;
    std::puts("Gameplay mouse-look, cursor hiding, menu controls, and Enter option selection passed.");
}
