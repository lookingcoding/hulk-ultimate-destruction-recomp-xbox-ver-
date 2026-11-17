// Isolated mouse math, keyboard lifecycle and real settings persistence. Never creates a window.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <toml++/toml.hpp>
#include "common/keyboard.h"
#include "common/camera.h"
#include "common/display_menu.h"
#include "common/language.h"
#include "common/settings_ui.h"
#include "core/input.h"
#include "core/log.h"

namespace mem { uint8_t* Base() { return nullptr; } }
namespace hulk {
void ReplayPad(int, input::Pad&) {}
void NoteSettingChanged() {}
bool ControlsCapturing() { return false; }
bool StepCameraDistance(int) { return false; }
void SetCameraInvert(bool, bool) {} bool GetCameraInvertX() { return false; } bool GetCameraInvertY() { return false; }
void SetPromptDevice(PromptDevice) {} PromptDevice GetPromptDevice() { return PromptDevice::Auto; }
const char* PromptDeviceName(PromptDevice) { return "auto"; }
void SetFovIncrease(int) {} int GetFovIncrease() { return 0; }
void SetRenderDistance(int) {} int GetRenderDistance() { return 100; }
void SetCameraDistance(int) {} int GetCameraDistance() { return 100; }
void SetLanguage(Language) {} Language GetLanguage() { return Language::English; }
const char* LanguageName(Language) { return "english"; }
bool LanguageFromName(const char*, Language&) { return false; }
}
namespace logx { void Write(Level, const char*, ...) {} }
namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); }
}
void Near(double actual, double expected, double tolerance, const char* what) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        ++failures; std::fprintf(stderr, "FAIL: %s (%.9f, expected %.9f)\n", what, actual, expected);
    }
}
// Reconstruct the commanded displacement, assuming a linear deadzone-adjusted native rate.
// This is a bridge invariant, NOT a measurement of the game's camera angles.
double Counts(int16_t stick, double seconds) {
    if (!stick) return 0;
    const double rate = double(std::abs(int(stick)) - hulk::MouseLook::kDeadzone) / (32767 - hulk::MouseLook::kDeadzone);
    return std::copysign(rate * hulk::MouseLook::kCountsPerSecond * seconds, stick);
}
void TestMotion() {
    using hulk::MouseLook;
    constexpr double dt = MouseLook::kSampleSeconds;
    for (int hz : {30, 60, 120, 240}) {
        MouseLook motion;
        double sum = 0;
        for (int i = 0; i < hz; ++i) {
            motion.AddRaw(600.0 / hz, 0);
            sum += Counts(motion.Step(1.0 / hz, 1, 1, false, false).x, 1.0 / hz);
        }
        Near(sum, 600, 0.04, "equal displacement across camera update rates");
    }
    for (int hz : {30, 60, 120, 240}) {
        MouseLook motion;
        motion.Poll(0, 1, 1, false, false);
        double sum = 0;
        for (int i = 1; i <= hz + hz / 10; ++i) {
            if (i <= hz) motion.AddRaw(600.0 / hz, 0);
            sum += Counts(motion.Poll(double(i) / hz, 1, 1, false, false).x, 1.0 / hz);
        }
        Near(sum, 600, .04, "cached poll stream preserves displacement across polling rates");
    }
    {
        MouseLook motion;
        motion.SetRawInputAvailable(true);
        motion.Poll(0, 1, 1, false, false);
        // 60.06 Hz must not become 30 Hz because each poll is 0.1% shy of a fresh 16.7ms gate.
        for (int i = 1; i <= 120; ++i) {
            motion.AddRaw(i % 2 ? 2 : 8, 0);
            const auto stick = motion.Poll(i * dt * .999, 1, 1, false, false);
            if (i == 1) Check(!stick.x, "first early poll waits for the anchored deadline");
            else if (i > 3) Near(Counts(stick.x, dt * .999), i % 2 ? 2 : 8, .002,
                "slightly-fast display does not halve mouse update cadence");
        }
    }
    MouseLook motion;
    motion.AddRaw(1, 1);
    auto s = motion.Step(dt, 1, 2, false, false);
    Check(s.x > MouseLook::kDeadzone && s.y < -MouseLook::kDeadzone, "one count clears camera deadzone on both axes");
    Near(Counts(s.y, dt), -2, .002, "Y sensitivity independent from X");
    motion.AddRaw(1, 1);
    s = motion.Step(dt, 3, 1, true, false);
    Near(Counts(s.x, dt), 3, .002, "X sensitivity independent from Y");
    Near(Counts(s.y, dt), 1, .002, "invert Y changes vertical sign only");
    motion.AddRaw(10, -10); motion.AddRaw(-10, 10);
    s = motion.Step(dt, 1, 1, false, false);
    Check(!s.x && !s.y, "opposing packets cancel before consumption");
    motion.AddRaw(0, 10);
    motion.Reset();
    s = motion.Step(dt, 1, 1, false, false);
    Check(!s.x && !s.y, "reset drops queued overflow and smoothing state");
    motion.AddRaw(10, 0);
    const auto first = motion.Poll(10, 1, 1, false, false);
    for (int i = 0; i < 16; ++i)
        Check(motion.Poll(10 + i * .001, 1, 1, false, false).x == first.x, "extra polls retain the current camera sample");
    Check(!motion.Poll(10 + dt, 1, 1, false, false).x, "sample expires in one interval after motion stops");
    motion.AddRaw(1, 0);
    Check(motion.Poll(11, 1, 1, false, false).x > MouseLook::kDeadzone, "new motion survives a gap longer than old 50ms timeout");
    motion.Reset();
    motion.Poll(20, 1, 1, false, false);
    motion.AddRaw(3, 0);
    Check(!motion.Poll(20.001, 1, 1, false, false).x, "motion arriving between samples waits without changing the cached sample");
    Near(Counts(motion.Poll(20 + dt, 1, 1, false, false).x, dt), 3, .002, "motion arriving between samples is retained");
}
void TestSourcesAndBounds() {
    using hulk::MouseLook;
    constexpr double dt = MouseLook::kSampleSeconds;
    MouseLook motion;
    motion.AddFallback(6, 0); motion.AddRaw(6, 0); motion.AddFallback(6, 0);
    Near(Counts(motion.Step(dt, 1, 1, false, false).x, dt), 6, .002, "first raw packet replaces queued duplicate fallback");
    motion.Reset(); motion.AddFallback(6, 0);
    Check(!motion.Step(dt, 1, 1, false, false).x, "raw ownership survives idle and focus resets");
    motion.SetRawInputAvailable(false); motion.AddFallback(7, 0);
    Near(Counts(motion.Step(dt, 1, 1, false, false).x, dt), 7, .002, "fallback available when raw registration fails");
    motion.SetRawInputAvailable(true); motion.AddFallback(7, 0);
    Check(!motion.Step(dt, 1, 1, false, false).x, "registered raw mode excludes fallback before the first raw packet");
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    Check(hulk::SanitizeMouseSensitivity(nan) == 1 && hulk::SanitizeMouseSensitivity(inf) == 5 &&
        hulk::SanitizeMouseSensitivity(-inf) == .1f, "nonfinite settings have defined bounded values");
    motion.AddRaw(nan, inf);
    auto s = motion.Step(dt, nan, inf, false, false);
    Check(!s.x && !s.y, "invalid device deltas do not poison motion");
    motion.AddRaw(1e300, -1e300);
    s = motion.Step(dt, inf, -inf, false, false);
    Check(s.x == 32767 && s.y == 32767, "huge finite deltas saturate safely without integer overflow");
    Check(!motion.Step(nan, 1, 1, false, false).x && !motion.Step(dt, 1, 1, false, false).x,
        "invalid elapsed time resets pending motion");
    for (double bad : {0., -1., double(inf)}) {
        motion.AddRaw(1, 1);
        s = motion.Step(bad, 1, 1, false, false);
        Check(!s.x && !s.y, "nonpositive/nonfinite step returns neutral");
    }
}
void TestImmediateStop() {
    using hulk::MouseLook;
    for (bool smoothing : {false, true}) for (int hz : {30, 60, 120, 240}) {
        MouseLook motion;
        const double dt = 1.0 / hz;
        motion.AddRaw(10000, -10000);
        const auto swipe = motion.Step(dt, 1, 1, false, smoothing);
        Check(swipe.x != 0 && swipe.y != 0, "fast swipe produces mouse look");
        motion.AddRaw(0, 1);
        const auto one_axis = motion.Step(dt, 1, 1, false, smoothing);
        Check(!one_axis.x && one_axis.y < 0, "stopped axis is neutral while the other axis moves");
        for (int i = 0; i < 20; ++i) {
            const auto stopped = motion.Step(dt, 1, 1, false, smoothing);
            Check(!stopped.x && !stopped.y, "no mouse overflow or smoothing rotation after swipe stops");
        }
        MouseLook polled;
        polled.AddRaw(10000, -10000);
        polled.Poll(100, 1, 1, false, smoothing);
        const auto idle = polled.Poll(100 + MouseLook::kSampleSeconds, 1, 1, false, smoothing);
        Check(!idle.x && !idle.y, "saturated cached poll becomes neutral at the next sample");
        motion.AddRaw(-1, 1);
        const auto reverse = motion.Step(dt, 1, 1, false, smoothing);
        Check(reverse.x < 0 && reverse.y < 0, "small reverse swipe is not swallowed by old motion");
    }
}
void TestSettings() {
    using namespace hulk;
    SetMouseSensitivity(1);
    auto legacy = toml::parse("mouse_sensitivity = 2.5\n");
    LoadCustomSettings(&legacy);
    Check(GetMouseSensitivityX() == 2.5f && GetMouseSensitivityY() == 2.5f, "legacy scalar migrates to both axes");
    auto explicit_axes = toml::parse("mouse_sensitivity = 4.0\nmouse_sensitivity_x = 0.5\nmouse_sensitivity_y = 1.5\nmouse_invert_y = true\n");
    LoadCustomSettings(&explicit_axes);
    Check(GetMouseSensitivityX() == .5f && GetMouseSensitivityY() == 1.5f && GetMouseInvertY(), "explicit axes override scalar and invert Y loads");
    toml::table saved;
    SaveCustomSettings(&saved);
    SetMouseSensitivity(1); SetMouseInvertY(false);
    LoadCustomSettings(&saved);
    Check(GetMouseSensitivityX() == .5f && GetMouseSensitivityY() == 1.5f && GetMouseInvertY(), "axis/invert settings round trip through real saver");
    auto mixed = toml::parse("mouse_sensitivity = 2.0\nmouse_sensitivity_y = 3.0\n");
    LoadCustomSettings(&mixed);
    Check(GetMouseSensitivityX() == 2 && GetMouseSensitivityY() == 3, "partial axis config uses legacy value for missing axis");
    auto bounds = toml::parse("mouse_sensitivity_x = nan\nmouse_sensitivity_y = inf\n");
    LoadCustomSettings(&bounds);
    Check(GetMouseSensitivityX() == 1 && GetMouseSensitivityY() == 5, "nonfinite TOML sensitivities sanitized");
    SetMouseSensitivityX(-100); SetMouseSensitivityY(100);
    Check(GetMouseSensitivityX() == .1f && GetMouseSensitivityY() == 5, "axis setters clamp UI values");
    SetMouseSensitivity(2);
    Check(GetMouseSensitivityX() == 2 && GetMouseSensitivityY() == 2 && GetMouseSensitivity() == 2, "legacy API continues to set both axes");
}
void TestLifecycle() {
    using namespace hulk;
    SetMouseSensitivity(1); SetMouseInvertY(false); SetMouseSmoothing(false);
    const auto message = [](UINT msg, LPARAM lp = 0) { KeyboardMessage(nullptr, msg, 0, lp, nullptr); };
    const auto poll = [] { input::Pad p{}; KeyboardPad(0, p); return p; };
    const auto move = [&] { message(WM_MOUSEMOVE, MAKELPARAM(100, 100)); message(WM_MOUSEMOVE, MAKELPARAM(110, 90)); };
    message(WM_KILLFOCUS); SetMenuModeForTest(-1); message(WM_SETFOCUS);
    move();
    auto p = poll();
    Check(p.rx > 0 && p.ry > 0, "fallback gameplay directions mapped through KeyboardPad");
    Check(poll().rx == p.rx, "KeyboardPad does not consume motion twice in quick succession");
    message(WM_KILLFOCUS);
    p = poll(); Check(!p.rx && !p.ry, "focus loss clears cached motion immediately");
    move(); message(WM_SETFOCUS);
    p = poll(); Check(!p.rx && !p.ry, "unfocused movement cannot leak after refocus");
    move(); SetMenuModeForTest(1);
    p = poll(); Check(!p.rx && !p.ry, "entering menu clears pending and cached motion");
    SetMenuModeForTest(-1);
    p = poll(); Check(!p.rx && !p.ry, "leaving menu does not revive previous motion");
    input::Pad physical{}; physical.connected = true; physical.rx = 1234; physical.ry = -2345;
    KeyboardPad(0, physical);
    Check(physical.rx == 1234 && physical.ry == -2345, "idle mouse preserves physical right stick");
    move(); message(WM_KILLFOCUS); message(WM_SETFOCUS);
    p = poll(); Check(!p.rx && !p.ry, "focus cycle clears movement queued without a poll");
    message(WM_KILLFOCUS); message(WM_SETFOCUS);
    message(WM_MOUSEMOVE, MAKELPARAM(0, 0)); message(WM_MOUSEMOVE, MAKELPARAM(30000, 30000));
    p = poll(); Check(p.rx == 32767 && p.ry == -32767, "large fallback swipe saturates this mouse sample");
    Sleep(20);
    physical.rx = 1234; physical.ry = -2345; physical.buttons = 0x80; physical.analog[6] = 170;
    KeyboardPad(0, physical);
    Check(physical.rx == 1234 && physical.ry == -2345 && physical.buttons == 0x80 && physical.analog[6] == 170,
          "controller stick/buttons survive mouse stop with no queued mouse rotation");

}
}
int main() {
    TestMotion(); TestSourcesAndBounds(); TestImmediateStop(); TestSettings(); TestLifecycle();
    if (!failures) std::puts("Mouse displacement bridge, settings migration and focus/menu lifecycle passed.");
    return failures ? 1 : 0;
}
