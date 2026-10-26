#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include "common/keyboard.h"
#include "common/replay.h"
#include "core/input.h"
#include "kernel/memory.h"

namespace hulk {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kPulseMinHold = std::chrono::milliseconds(45);
constexpr auto kPulseMinGap = std::chrono::milliseconds(45);
constexpr int kMinPollsPerPhase = 2;
constexpr int kMenuMoveStepPixels = 40;
constexpr size_t kMaxQueuedSteps = 8;
constexpr UINT kSyncCursorMsg = WM_APP + 42;
constexpr float kCameraDeadzone = 7500.0f;
constexpr float kCameraSensitivity = 950.0f;

struct ButtonLatch {
    bool held = false;
    int polls_remaining = 0;
    Clock::time_point release_after{};

    void Press() {
        held = true;
        polls_remaining = kMinPollsPerPhase;
        release_after = Clock::now() + kPulseMinHold;
    }
    void Release() {
        held = false;
    }
    void Reset() {
        held = false;
        polls_remaining = 0;
        release_after = {};
    }
    bool Sample(Clock::time_point now) {
        if (held) return true;
        if (polls_remaining <= 0) return false;
        --polls_remaining;
        if (polls_remaining == 0 && now < release_after) {
            polls_remaining = 1;
        }
        return true;
    }
};

struct StepQueue {
    std::deque<uint16_t> masks;
    uint16_t active_mask = 0;
    bool in_gap = false;
    int polls_remaining = 0;
    Clock::time_point phase_until{};

    void Reset() {
        masks.clear();
        active_mask = 0;
        in_gap = false;
        polls_remaining = 0;
        phase_until = {};
    }

    void Push(uint16_t mask, size_t limit = kMaxQueuedSteps) {
        if (masks.size() < limit) masks.push_back(mask);
    }

    bool Busy() const {
        return active_mask != 0 || in_gap || !masks.empty();
    }

    uint16_t Sample(Clock::time_point now) {
        if (in_gap) {
            if (polls_remaining > 0) --polls_remaining;
            if (polls_remaining == 0 && now >= phase_until) {
                in_gap = false;
            } else {
                return 0;
            }
        }
        if (active_mask == 0) {
            if (masks.empty()) return 0;
            active_mask = masks.front();
            masks.pop_front();
            polls_remaining = kMinPollsPerPhase;
            phase_until = now + kPulseMinHold;
        }
        uint16_t out = active_mask;
        if (polls_remaining > 0) --polls_remaining;
        if (polls_remaining == 0 && now >= phase_until) {
            active_mask = 0;
            in_gap = true;
            polls_remaining = kMinPollsPerPhase;
            phase_until = now + kPulseMinGap;
        }
        return out;
    }
};

std::array<std::atomic<bool>, 256> keys{};
std::atomic<HWND> game_hwnd{nullptr};
std::atomic<bool> window_focused{true};
std::atomic<int> test_mode_override{0};
std::atomic<bool> cursor_hidden{false};
std::atomic<int> last_polled_menu_state{-1};
std::atomic<bool> raw_input_registered{false};

// Configurable PC controls
std::atomic<float> mouse_sensitivity{1.0f};
std::atomic<bool> mouse_invert_y{false};
std::atomic<bool> mouse_smoothing{false};
std::atomic<bool> settings_open{false};

std::mutex mouse_mutex;
ButtonLatch mouse_left{};
ButtonLatch mouse_right{};
StepQueue dpad_steps{};
int wheel_v_accum = 0;
int wheel_h_accum = 0;
bool has_cursor_pos = false;
int last_cursor_x = 0;
int last_cursor_y = 0;
int move_accum_x = 0;
int move_accum_y = 0;
float look_dx = 0.0f;
float look_dy = 0.0f;
bool has_raw_delta = false;
Clock::time_point last_look_time{};

bool Down(unsigned key) { return keys[key].load(std::memory_order_relaxed); }

bool ReadGuestU32(const uint8_t* base, uint32_t va, uint32_t& out) {
    if (!base || va < 0x00010000u || va > 0xDFFFFFFCu) return false;
    __try {
        std::memcpy(&out, base + va, sizeof(uint32_t));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Queries CGameFlowManager state in guest memory:
// 1 = GS_FRONTEND, 2 = GS_PREGAME, 3 = GS_PRESSSTART, 4 = GS_GAME (Active Gameplay),
// 5 = GS_POSTGAME, 6 = GS_PAUSE, 7 = GS_FMV, 8 = GS_QUIT.
bool IsInMenuState() {
    const int override_mode = test_mode_override.load(std::memory_order_relaxed);
    if (override_mode != 0) return override_mode > 0;
    const uint8_t* base = mem::Base();
    if (!base) return true;
    uint32_t app_ptr = 0, flow_mgr = 0, state_obj = 0, req_state = 0, cur_state = 0;
    if (!ReadGuestU32(base, 0x004DF840u, app_ptr) || !app_ptr) return true;
    if (!ReadGuestU32(base, app_ptr + 0x54u, flow_mgr) || !flow_mgr) return true;
    if (!ReadGuestU32(base, flow_mgr + 0x04u, state_obj) || !state_obj) return true;
    if (!ReadGuestU32(base, state_obj + 0x00u, req_state)) return true;
    if (!ReadGuestU32(base, state_obj + 0x04u, cur_state)) return true;
    return !(cur_state == 4u && req_state == 4u);
}

bool CenterCursorInWindow(HWND hwnd, POINT& center_client) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc) || rc.right <= rc.left || rc.bottom <= rc.top) return false;
    center_client.x = (rc.right - rc.left) / 2;
    center_client.y = (rc.bottom - rc.top) / 2;
    POINT top_left{rc.left, rc.top};
    POINT bottom_right{rc.right, rc.bottom};
    POINT center_screen = center_client;
    if (!ClientToScreen(hwnd, &top_left) || !ClientToScreen(hwnd, &bottom_right) || !ClientToScreen(hwnd, &center_screen)) {
        return false;
    }
    RECT clip_rc{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    ClipCursor(&clip_rc);
    SetCursorPos(center_screen.x, center_screen.y);
    return true;
}

void ApplyCursorVisibilityOnWindowThread(HWND hwnd, bool in_menu) {
    const bool should_hide = !in_menu && !settings_open.load(std::memory_order_relaxed) && window_focused.load(std::memory_order_relaxed);
    const bool was_hidden = cursor_hidden.exchange(should_hide, std::memory_order_relaxed);
    if (should_hide) {
        if (!was_hidden) {
            if (hwnd && IsWindow(hwnd)) {
                while (ShowCursor(FALSE) >= 0) {}
                SetCursor(nullptr);
            }
            std::lock_guard lock(mouse_mutex);
            dpad_steps.Reset();
            move_accum_x = 0;
            move_accum_y = 0;
            POINT center{};
            if (CenterCursorInWindow(hwnd, center)) {
                has_cursor_pos = true;
                last_cursor_x = center.x;
                last_cursor_y = center.y;
            } else {
                has_cursor_pos = false;
            }
        }
    } else {
        if (was_hidden) {
            if (hwnd && IsWindow(hwnd)) {
                ClipCursor(nullptr);
                while (ShowCursor(TRUE) < 0) {}
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            } else {
                ClipCursor(nullptr);
            }
            std::lock_guard lock(mouse_mutex);
            look_dx = 0.0f;
            look_dy = 0.0f;
            has_cursor_pos = false;
        }
    }
}

int16_t StickFromLookDelta(float delta) {
    if (std::abs(delta) < 0.1f) return 0;
    const float sign = delta < 0.0f ? -1.0f : 1.0f;
    const float sens = mouse_sensitivity.load(std::memory_order_relaxed);
    const float abs_d = std::abs(delta) * sens;

    // Smooth deadzone blending:
    // Hulk's camera engine requires stick values above ~7500 to rotate.
    // Instead of a step jump from 0 to 8450 (which made micro-aiming impossible and jerky),
    // blend deadzone smoothly over the initial motion, then scale linearly.
    const float blend = std::min(1.0f, abs_d / 2.5f);
    const float deadzone = kCameraDeadzone * blend;
    const float dynamic_rate = abs_d * kCameraSensitivity;
    const float mag = deadzone + dynamic_rate;
    return static_cast<int16_t>(std::clamp(sign * mag, -32767.0f, 32767.0f));
}

void Clear() {
    for (auto& key : keys) key.store(false, std::memory_order_relaxed);
    std::lock_guard lock(mouse_mutex);
    mouse_left.Reset();
    mouse_right.Reset();
    dpad_steps.Reset();
    wheel_v_accum = 0;
    wheel_h_accum = 0;
    has_cursor_pos = false;
    move_accum_x = 0;
    move_accum_y = 0;
    look_dx = 0.0f;
    look_dy = 0.0f;
    has_raw_delta = false;
}
}  // namespace

void SetMouseSensitivity(float s) { mouse_sensitivity.store(std::clamp(s, 0.1f, 5.0f), std::memory_order_relaxed); }
float GetMouseSensitivity() { return mouse_sensitivity.load(std::memory_order_relaxed); }
void SetMouseInvertY(bool invert) { mouse_invert_y.store(invert, std::memory_order_relaxed); }
bool GetMouseInvertY() { return mouse_invert_y.load(std::memory_order_relaxed); }
void SetMouseSmoothing(bool smoothing) { mouse_smoothing.store(smoothing, std::memory_order_relaxed); }
bool GetMouseSmoothing() { return mouse_smoothing.load(std::memory_order_relaxed); }

bool IsSettingsOpen() { return settings_open.load(std::memory_order_relaxed); }
void SetSettingsOpen(bool open) {
    settings_open.store(open, std::memory_order_relaxed);
    if (HWND hwnd = game_hwnd.load(std::memory_order_relaxed); hwnd && IsWindow(hwnd)) {
        ApplyCursorVisibilityOnWindowThread(hwnd, IsInMenuState());
    }
}
void ToggleSettings() {
    SetSettingsOpen(!IsSettingsOpen());
}

void SetGameWindow(void* hwnd) {
    HWND w = static_cast<HWND>(hwnd);
    game_hwnd.store(w, std::memory_order_relaxed);
    if (w && IsWindow(w) && !raw_input_registered.exchange(true, std::memory_order_relaxed)) {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;  // Generic Desktop Controls
        rid.usUsage = 0x02;      // Mouse
        rid.dwFlags = 0;
        rid.hwndTarget = w;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
}

void SetMenuModeForTest(int mode) {
    test_mode_override.store(mode, std::memory_order_relaxed);
    ApplyCursorVisibilityOnWindowThread(game_hwnd.load(std::memory_order_relaxed), IsInMenuState());
}

bool IsCursorHiddenForTest() {
    return cursor_hidden.load(std::memory_order_relaxed);
}

bool KeyboardMessage(void* hwnd_ptr, uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result) {
    HWND hwnd = static_cast<HWND>(hwnd_ptr);
    if (hwnd) game_hwnd.store(hwnd, std::memory_order_relaxed);
    else hwnd = game_hwnd.load(std::memory_order_relaxed);

    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wp) ||
        (message == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE)) {
        window_focused.store(false, std::memory_order_relaxed);
        Clear();
        ApplyCursorVisibilityOnWindowThread(hwnd, true);
        return false;
    }
    if ((message == WM_ACTIVATEAPP && wp) || (message == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE) ||
        message == WM_SETFOCUS) {
        window_focused.store(true, std::memory_order_relaxed);
        ApplyCursorVisibilityOnWindowThread(hwnd, IsInMenuState());
    }

    if (message == kSyncCursorMsg) {
        ApplyCursorVisibilityOnWindowThread(hwnd, wp != 0);
        if (result) *result = 0;
        return true;
    }

    const bool in_menu = IsInMenuState() || settings_open.load(std::memory_order_relaxed);
    ApplyCursorVisibilityOnWindowThread(hwnd, in_menu);

    if (message == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        if (cursor_hidden.load(std::memory_order_relaxed)) {
            if (hwnd && IsWindow(hwnd)) SetCursor(nullptr);
            if (result) *result = 1;
            return true;
        }
        if (hwnd && IsWindow(hwnd)) SetCursor(LoadCursor(nullptr, IDC_ARROW));
        if (result) *result = 1;
        return true;
    }

    if (message == WM_KEYDOWN) {
        if (wp == VK_F1 || wp == VK_OEM_3) {  // F1 or `~`
            ToggleSettings();
            if (result) *result = 0;
            return true;
        }
        if (wp == VK_ESCAPE && settings_open.load(std::memory_order_relaxed)) {
            SetSettingsOpen(false);
            if (result) *result = 0;
            return true;
        }
    }

    if (message == WM_KEYDOWN || message == WM_KEYUP) {
        if (wp < keys.size()) {
            keys[wp].store(message == WM_KEYDOWN, std::memory_order_relaxed);
        }
        return false;
    }

    if (message == WM_INPUT) {
        if (!window_focused.load(std::memory_order_relaxed) || in_menu) {
            return false;
        }
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
            if (raw.header.dwType == RIM_TYPEMOUSE) {
                int dx = 0, dy = 0;
                if ((raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                    dx = raw.data.mouse.lLastX;
                    dy = raw.data.mouse.lLastY;
                }
                if (dx != 0 || dy != 0) {
                    std::lock_guard lock(mouse_mutex);
                    look_dx += static_cast<float>(dx);
                    look_dy += static_cast<float>(dy);
                    last_look_time = Clock::now();
                    has_raw_delta = true;
                }
            }
        }
        return false;
    }

    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            std::lock_guard lock(mouse_mutex);
            mouse_left.Press();
            break;
        }
        case WM_LBUTTONUP: {
            std::lock_guard lock(mouse_mutex);
            mouse_left.Release();
            break;
        }
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK: {
            std::lock_guard lock(mouse_mutex);
            mouse_right.Press();
            break;
        }
        case WM_RBUTTONUP: {
            std::lock_guard lock(mouse_mutex);
            mouse_right.Release();
            break;
        }
        case WM_MOUSEWHEEL: {
            if (!in_menu) break;
            std::lock_guard lock(mouse_mutex);
            wheel_v_accum += GET_WHEEL_DELTA_WPARAM(wp);
            const bool horizontal = (GET_KEYSTATE_WPARAM(wp) & MK_SHIFT) != 0 || Down(VK_SHIFT);
            while (wheel_v_accum >= WHEEL_DELTA) {
                wheel_v_accum -= WHEEL_DELTA;
                dpad_steps.Push(horizontal ? 0x04u : 0x01u);  // Left or Up
            }
            while (wheel_v_accum <= -WHEEL_DELTA) {
                wheel_v_accum += WHEEL_DELTA;
                dpad_steps.Push(horizontal ? 0x08u : 0x02u);  // Right or Down
            }
            break;
        }
        case WM_MOUSEHWHEEL: {
            if (!in_menu) break;
            std::lock_guard lock(mouse_mutex);
            wheel_h_accum += GET_WHEEL_DELTA_WPARAM(wp);
            while (wheel_h_accum >= WHEEL_DELTA) {
                wheel_h_accum -= WHEEL_DELTA;
                dpad_steps.Push(0x08u);  // Right
            }
            while (wheel_h_accum <= -WHEEL_DELTA) {
                wheel_h_accum += WHEEL_DELTA;
                dpad_steps.Push(0x04u);  // Left
            }
            break;
        }
        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lp);
            const int y = GET_Y_LPARAM(lp);
            std::lock_guard lock(mouse_mutex);
            if (!has_cursor_pos) {
                has_cursor_pos = true;
                last_cursor_x = x;
                last_cursor_y = y;
                break;
            }
            const int dx = x - last_cursor_x;
            const int dy = y - last_cursor_y;
            last_cursor_x = x;
            last_cursor_y = y;
            if (dx == 0 && dy == 0) break;

            if (in_menu) {
                if (!settings_open.load(std::memory_order_relaxed)) {
                    move_accum_x += dx;
                    move_accum_y += dy;
                    if (std::abs(move_accum_y) >= kMenuMoveStepPixels && std::abs(move_accum_y) >= std::abs(move_accum_x)) {
                        if (!dpad_steps.Busy()) {
                            dpad_steps.Push(move_accum_y < 0 ? 0x01u : 0x02u, 1);  // Up or Down
                        }
                        move_accum_x = 0;
                        move_accum_y = 0;
                    } else if (std::abs(move_accum_x) >= kMenuMoveStepPixels && std::abs(move_accum_x) > std::abs(move_accum_y)) {
                        if (!dpad_steps.Busy()) {
                            dpad_steps.Push(move_accum_x < 0 ? 0x04u : 0x08u, 1);  // Left or Right
                        }
                        move_accum_x = 0;
                        move_accum_y = 0;
                    }
                }
            } else if (window_focused.load(std::memory_order_relaxed)) {
                if (!has_raw_delta) {
                    look_dx += static_cast<float>(dx);
                    look_dy += static_cast<float>(dy);
                    last_look_time = Clock::now();
                }
                POINT center{};
                if (CenterCursorInWindow(hwnd, center)) {
                    last_cursor_x = center.x;
                    last_cursor_y = center.y;
                }
            }
            break;
        }
        default:
            break;
    }
    return false;
}

void KeyboardPad(int port, input::Pad& pad) {
    if (port != 0) return;
    pad.connected = true;

    if (settings_open.load(std::memory_order_relaxed)) {
        // While in settings overlay, don't pass keyboard/mouse gameplay inputs
        return;
    }

    const bool in_menu = IsInMenuState();
    const int menu_flag = in_menu ? 1 : 0;
    if (last_polled_menu_state.exchange(menu_flag, std::memory_order_relaxed) != menu_flag) {
        if (HWND hwnd = game_hwnd.load(std::memory_order_relaxed); hwnd && IsWindow(hwnd)) {
            PostMessageW(hwnd, kSyncCursorMsg, static_cast<WPARAM>(menu_flag), 0);
        }
    }

    // Strictly one keybind per action:
    // D-pad: Arrow keys (Up / Down / Left / Right)
    // Start (Menu / Pause): Esc
    // Back (Map / Objectives): Tab
    // Left Stick Click (Reset Camera): C
    // Right Stick Click (Taunt): V
    if (Down(VK_UP)) pad.buttons |= 0x01u;
    if (Down(VK_DOWN)) pad.buttons |= 0x02u;
    if (Down(VK_LEFT)) pad.buttons |= 0x04u;
    if (Down(VK_RIGHT)) pad.buttons |= 0x08u;
    if (Down(VK_ESCAPE)) pad.buttons |= 0x10u;
    if (Down(VK_TAB)) pad.buttons |= 0x20u;
    if (Down('C')) pad.buttons |= 0x40u;
    if (Down('V')) pad.buttons |= 0x80u;

    // Analog buttons (0-255), one keybind each:
    // [0] A (Jump / Charge Jump): Space
    // [1] B (Grab / Pick Up / Throw): Q
    // [2] X (Punch / Attack): Left Click (in gameplay)
    // [3] Y (Critical Mass / Ground Slam / Special): E
    // [4] Black (Smash Camera): 2
    // [5] White (Cycle Target): 1
    // [6] Left Trigger (Target Lock-On / Guard): Right Click (in gameplay)
    // [7] Right Trigger (Sprint / Wall Run / Dodge): Left Shift
    if (Down(VK_SPACE)) pad.analog[0] = 255;
    if (Down('Q')) pad.analog[1] = 255;
    if (Down('E')) pad.analog[3] = 255;
    if (Down('2')) pad.analog[4] = 255;
    if (Down('1')) pad.analog[5] = 255;
    if (Down(VK_SHIFT)) pad.analog[7] = 255;

    // Left stick movement: WASD
    auto axis = [](unsigned positive, unsigned negative) -> int16_t {
        return int16_t((int(Down(positive)) - int(Down(negative))) * 32767);
    };
    if (auto x = axis('D', 'A')) pad.lx = x;
    if (auto y = axis('W', 'S')) pad.ly = y;

    {
        std::lock_guard lock(mouse_mutex);
        const auto now = Clock::now();
        const bool left_pressed = mouse_left.Sample(now);
        const bool right_pressed = mouse_right.Sample(now);

        if (in_menu) {
            // In menus: Left Click = A (Select), Right Click = B (Back), Wheel/Move = D-Pad
            if (left_pressed) pad.analog[0] = 255;
            if (right_pressed) pad.analog[1] = 255;
            pad.buttons |= dpad_steps.Sample(now);
            look_dx = 0.0f;
            look_dy = 0.0f;
            has_raw_delta = false;
        } else {
            // In gameplay: Left Click = X (Punch), Right Click = LT (Lock-On), Mouse Move = Right Stick Camera
            if (left_pressed) pad.analog[2] = 255;
            if (right_pressed) pad.analog[6] = 255;

            if (now - last_look_time > std::chrono::milliseconds(50)) {
                look_dx = 0.0f;
                look_dy = 0.0f;
                has_raw_delta = false;
            } else {
                float dy = look_dy;
                if (mouse_invert_y.load(std::memory_order_relaxed)) {
                    dy = -dy;
                }
                if (int16_t rx = StickFromLookDelta(look_dx)) pad.rx = rx;
                if (int16_t ry = StickFromLookDelta(-dy)) pad.ry = ry;

                if (mouse_smoothing.load(std::memory_order_relaxed)) {
                    // Subtle 1-frame deceleration filter if smoothing is requested
                    look_dx *= 0.20f;
                    look_dy *= 0.20f;
                    if (std::abs(look_dx) < 0.25f) look_dx = 0.0f;
                    if (std::abs(look_dy) < 0.25f) look_dy = 0.0f;
                } else {
                    // True 1:1 PC feel: instant stop, no lingering rotation!
                    look_dx = 0.0f;
                    look_dy = 0.0f;
                }
                has_raw_delta = false;
            }
        }
    }

    ReplayPad(port, pad);
}
}  // namespace hulk
