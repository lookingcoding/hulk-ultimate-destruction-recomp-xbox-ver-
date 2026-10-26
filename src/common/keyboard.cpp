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
#include "common/settings_ui.h"
#include "core/input.h"
#include "kernel/memory.h"

namespace hulk {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kPulseMinHold = std::chrono::milliseconds(25);
constexpr auto kPulseMinGap = std::chrono::milliseconds(20);
constexpr int kMinPollsPerPhase = 1;
constexpr int kMenuMoveStepPixels = 40;
constexpr size_t kMaxQueuedSteps = 4;
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

// Configurable PC controls (persisted in settings.toml)
std::atomic<float> mouse_sensitivity{1.0f};
std::atomic<bool> mouse_invert_y{false};
std::atomic<bool> mouse_smoothing{false};

// Menu selection tracking
std::atomic<int> current_menu_selected_row{0};
std::atomic<uint32_t> last_menu_flow_state{0};

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
uint32_t GetGameFlowState() {
    const uint8_t* base = mem::Base();
    if (!base) return 0;
    uint32_t app_ptr = 0, flow_mgr = 0, state_obj = 0, cur_state = 0;
    if (!ReadGuestU32(base, 0x004DF840u, app_ptr) || !app_ptr) return 0;
    if (!ReadGuestU32(base, app_ptr + 0x54u, flow_mgr) || !flow_mgr) return 0;
    if (!ReadGuestU32(base, flow_mgr + 0x04u, state_obj) || !state_obj) return 0;
    if (!ReadGuestU32(base, state_obj + 0x04u, cur_state)) return 0;
    return cur_state;
}

// Returns the menu row index under the cursor, or -1 if outside menu rows.
// Native menu items start at Y ~110 and are spaced ~31.5px apart in 640x480 space.
int GetHoveredMenuRow(int cursor_x, int cursor_y, int client_w, int client_h) {
    if (client_w <= 0 || client_h <= 0) return -1;
    const float vx = (static_cast<float>(cursor_x) / static_cast<float>(client_w)) * 640.0f;
    const float vy = (static_cast<float>(cursor_y) / static_cast<float>(client_h)) * 480.0f;

    // Menu text & highlight bars span across the center of the screen
    if (vx >= 50.0f && vx <= 590.0f) {
        constexpr float kStartY = 110.0f;
        constexpr float kRowH = 31.5f;
        if (vy >= kStartY && vy < kStartY + 6 * kRowH) {
            int row = static_cast<int>((vy - kStartY) / kRowH);
            return std::clamp(row, 0, 5);
        }
    }
    return -1;
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
    const bool should_hide = !in_menu && window_focused.load(std::memory_order_relaxed);
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

int GetCurrentMenuSelectedRow() {
    return current_menu_selected_row.load(std::memory_order_relaxed);
}

void SetCurrentMenuSelectedRow(int row) {
    current_menu_selected_row.store(row, std::memory_order_relaxed);
}

bool IsInMenuState() {
    if (IsDisplayOptionsOpen()) return true;
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

bool IsCurrentScreenOptions() {
    const uint8_t* base = mem::Base();
    if (!base) return false;
    uint32_t flag = 0;
    if (ReadGuestU32(base, 0x004E436Cu, flag) && (flag & 0xFF) == 1) {
        return true;
    }
    uint32_t screen_mgr = 0;
    if (!ReadGuestU32(base, 0x004E2BA0u, screen_mgr) || !screen_mgr) return false;
    uint32_t cur_screen = 0;
    if (!ReadGuestU32(base, screen_mgr + 0x175Cu, cur_screen) || !cur_screen) return false;

    for (uint32_t off = 0; off < 0x80u; off += 4) {
        uint32_t val = 0;
        if (ReadGuestU32(base, cur_screen + off, val)) {
            if (val == 0x0040A9F0u) return true;
            if (val >= 0x00010000u && val < 0x00580000u) {
                if (std::memcmp(base + val, "options\0", 8) == 0) return true;
            }
        }
    }
    return false;
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

    const bool in_menu = IsInMenuState();
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

    // Determine window client dimensions
    int client_w = 640, client_h = 480;
    if (hwnd && IsWindow(hwnd)) {
        RECT rc{};
        if (GetClientRect(hwnd, &rc)) {
            if (rc.right > rc.left) client_w = rc.right - rc.left;
            if (rc.bottom > rc.top) client_h = rc.bottom - rc.top;
        }
    }

    // 1. If 1:1 Display Options screen is active, route all menu interaction directly
    if (IsDisplayOptionsOpen()) {
        if (message == WM_KEYDOWN || message == WM_KEYUP) {
            if (HandleDisplayInputKey(static_cast<uint32_t>(wp), message == WM_KEYDOWN)) {
                if (result) *result = 0;
                return true;
            }
        } else if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) {
            const int x = GET_X_LPARAM(lp);
            const int y = GET_Y_LPARAM(lp);
            if (HandleDisplayClick(x, y, client_w, client_h, message == WM_RBUTTONDOWN)) {
                if (result) *result = 0;
                return true;
            }
        } else if (message == WM_MOUSEWHEEL) {
            HandleDisplayWheel(GET_WHEEL_DELTA_WPARAM(wp));
            if (result) *result = 0;
            return true;
        } else if (message == WM_MOUSEMOVE) {
            HandleDisplayHover(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), client_w, client_h);
            return false;
        }
        return false;
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
            if (in_menu) {
                const int hovered = GetHoveredMenuRow(last_cursor_x, last_cursor_y, client_w, client_h);
                if (hovered >= 0) {
                    current_menu_selected_row.store(hovered, std::memory_order_relaxed);
                    // If in the native Options menu and Row 3 (Display Options) is clicked, open it directly!
                    if (hovered == 3 && IsCurrentScreenOptions()) {
                        SetDisplayOptionsOpen(true);
                        return true;
                    }
                }
            }
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
                dpad_steps.Push(horizontal ? 0x04u : 0x01u);
            }
            while (wheel_v_accum <= -WHEEL_DELTA) {
                wheel_v_accum += WHEEL_DELTA;
                dpad_steps.Push(horizontal ? 0x08u : 0x02u);
            }
            break;
        }
        case WM_MOUSEHWHEEL: {
            if (!in_menu) break;
            std::lock_guard lock(mouse_mutex);
            wheel_h_accum += GET_WHEEL_DELTA_WPARAM(wp);
            while (wheel_h_accum >= WHEEL_DELTA) {
                wheel_h_accum -= WHEEL_DELTA;
                dpad_steps.Push(0x08u);
            }
            while (wheel_h_accum <= -WHEEL_DELTA) {
                wheel_h_accum += WHEEL_DELTA;
                dpad_steps.Push(0x04u);
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
                const int hovered = GetHoveredMenuRow(x, y, client_w, client_h);
                if (hovered >= 0) {
                    const int sel = current_menu_selected_row.load(std::memory_order_relaxed);
                    if (hovered != sel) {
                        dpad_steps.Reset();
                        const int diff = hovered - sel;
                        if (diff > 0) {
                            dpad_steps.Push(0x02u);
                        } else if (diff < 0) {
                            dpad_steps.Push(0x01u);
                        }
                        current_menu_selected_row.store(hovered, std::memory_order_relaxed);
                    }
                } else if (test_mode_override.load(std::memory_order_relaxed) > 0) {
                    move_accum_y += dy;
                    if (move_accum_y <= -kMenuMoveStepPixels) {
                        dpad_steps.Push(0x01u);
                        move_accum_y = 0;
                    } else if (move_accum_y >= kMenuMoveStepPixels) {
                        dpad_steps.Push(0x02u);
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

    if (IsDisplayOptionsOpen()) {
        // While Display Options is open, all interaction is handled natively by the sub-screen
        return;
    }

    const bool in_menu = IsInMenuState();
    const int menu_flag = in_menu ? 1 : 0;
    if (last_polled_menu_state.exchange(menu_flag, std::memory_order_relaxed) != menu_flag) {
        if (HWND hwnd = game_hwnd.load(std::memory_order_relaxed); hwnd && IsWindow(hwnd)) {
            PostMessageW(hwnd, kSyncCursorMsg, static_cast<WPARAM>(menu_flag), 0);
        }
    }

    const uint32_t flow_state = GetGameFlowState();
    if (last_menu_flow_state.exchange(flow_state, std::memory_order_relaxed) != flow_state) {
        current_menu_selected_row.store(0, std::memory_order_relaxed);
    }

    // Keyboard navigation tracking in menus
    if (in_menu) {
        const bool is_options = IsCurrentScreenOptions();
        const int max_row = is_options ? 3 : 5;

        if (Down(VK_UP)) {
            int cur = current_menu_selected_row.load(std::memory_order_relaxed);
            if (cur > 0) current_menu_selected_row.store(cur - 1, std::memory_order_relaxed);
        }
        if (Down(VK_DOWN)) {
            int cur = current_menu_selected_row.load(std::memory_order_relaxed);
            if (cur < max_row) current_menu_selected_row.store(cur + 1, std::memory_order_relaxed);
        }

        // Activating Row 3 in the native Options menu opens Display Options!
        if (is_options && (Down(VK_SPACE) || Down(VK_RETURN))) {
            int cur = current_menu_selected_row.load(std::memory_order_relaxed);
            if (cur == 3) {
                SetDisplayOptionsOpen(true);
                return;
            }
        }
    }

    // Single keybind per action:
    if (Down(VK_UP)) pad.buttons |= 0x01u;
    if (Down(VK_DOWN)) pad.buttons |= 0x02u;
    if (Down(VK_LEFT)) pad.buttons |= 0x04u;
    if (Down(VK_RIGHT)) pad.buttons |= 0x08u;
    if (Down(VK_ESCAPE)) pad.buttons |= 0x10u;
    if (Down(VK_TAB)) pad.buttons |= 0x20u;
    if (Down('C')) pad.buttons |= 0x40u;
    if (Down('V')) pad.buttons |= 0x80u;

    if (Down(VK_SPACE) || Down(VK_RETURN)) pad.analog[0] = 255;
    if (Down('Q')) pad.analog[1] = 255;
    if (Down('E')) pad.analog[3] = 255;
    if (Down('2')) pad.analog[4] = 255;
    if (Down('1')) pad.analog[5] = 255;
    if (Down(VK_SHIFT)) pad.analog[7] = 255;

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
            if (left_pressed) pad.analog[0] = 255;
            if (right_pressed) pad.analog[1] = 255;
            pad.buttons |= dpad_steps.Sample(now);
            look_dx = 0.0f;
            look_dy = 0.0f;
            has_raw_delta = false;
        } else {
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
                    look_dx *= 0.20f;
                    look_dy *= 0.20f;
                    if (std::abs(look_dx) < 0.25f) look_dx = 0.0f;
                    if (std::abs(look_dy) < 0.25f) look_dy = 0.0f;
                } else {
                    look_dx = 0.0f;
                    look_dy = 0.0f;
                }
            }
        }
    }

    ReplayPad(port, pad);
}

}  // namespace hulk
