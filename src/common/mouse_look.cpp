#include "common/mouse_look.h"
#include <algorithm>
#include <cmath>

namespace hulk {
float SanitizeMouseSensitivity(float value) {
    if (std::isnan(value)) return kMouseSensitivityDefault;
    return std::clamp(value, kMouseSensitivityMin, kMouseSensitivityMax);
}

void MouseLook::Reset() {
    pending_x_ = pending_y_ = filtered_x_ = filtered_y_ = last_poll_ = next_sample_ = 0;
    sampled_ = false;
    cached_ = {};
}
void MouseLook::SetRawInputAvailable(bool available) {
    if (raw_input_ != available) {
        pending_x_ = pending_y_ = filtered_x_ = filtered_y_ = 0;
        cached_ = {};
        // Discard the other source's motion without changing the elapsed-time baseline.
    }
    raw_input_ = available;
}
void MouseLook::Add(double dx, double dy) {
    // Many orders above any real swipe, while keeping arithmetic finite under corrupt/synthetic input.
    constexpr double kMaxPending = 1e12;
    if (std::isfinite(dx)) pending_x_ = std::clamp(pending_x_ + dx, -kMaxPending, kMaxPending);
    if (std::isfinite(dy)) pending_y_ = std::clamp(pending_y_ + dy, -kMaxPending, kMaxPending);
}
void MouseLook::AddRaw(double dx, double dy) {
    // A successfully received relative packet establishes raw ownership even if registration was unavailable.
    SetRawInputAvailable(true);
    Add(dx, dy);
}
void MouseLook::AddFallback(double dx, double dy) {
    if (!raw_input_) Add(dx, dy);
}

MouseStick MouseLook::Step(double seconds, float sensitivity_x, float sensitivity_y, bool invert_y, bool smoothing) {
    if (!std::isfinite(seconds) || seconds <= 0) { Reset(); return {}; }
    const auto axis = [seconds, smoothing](double& pending, double& filtered, float sensitivity) -> int16_t {
        const double counts = pending;
        pending = 0; // A mouse sample never queues overflow for a later camera update.
        if (std::abs(counts) < 1e-6) { filtered = 0; return 0; }
        const double rate = std::clamp(counts * SanitizeMouseSensitivity(sensitivity) /
                                       (kCountsPerSecond * seconds), -1.0, 1.0);
        if (rate * filtered < 0) filtered = 0;
        filtered = smoothing ? filtered + (-std::expm1(-seconds / 0.025)) * (rate - filtered) : rate;
        const int magnitude = std::clamp(int(std::lround(kDeadzone + (32767 - kDeadzone) * std::abs(filtered))), kDeadzone + 1, 32767);
        return static_cast<int16_t>(filtered < 0 ? -magnitude : magnitude);
    };
    cached_.x = axis(pending_x_, filtered_x_, sensitivity_x);
    cached_.y = axis(pending_y_, filtered_y_, sensitivity_y);
    if (!invert_y) cached_.y = static_cast<int16_t>(-cached_.y);  // screen y is down, Xbox stick y is up
    return cached_;
}

MouseStick MouseLook::Poll(double now_seconds, float sensitivity_x, float sensitivity_y, bool invert_y, bool smoothing) {
    if (!std::isfinite(now_seconds)) { Reset(); return {}; }
    double elapsed = kSampleSeconds;
    if (sampled_) {
        elapsed = now_seconds - last_poll_;
        if (elapsed < 0) { Reset(); return {}; }
        if (now_seconds + 1e-9 < next_sample_) return cached_;
        // Anchor deadlines instead of restarting a 16.7ms gate at each poll. The latter halves
        // the update cadence when a display happens to run slightly faster than 60 Hz.
        const double intervals = std::floor(std::max(0.0, now_seconds - next_sample_) / kSampleSeconds + 1e-7) + 1;
        next_sample_ += intervals * kSampleSeconds;
    } else {
        next_sample_ = now_seconds + kSampleSeconds;
    }
    sampled_ = true;
    last_poll_ = now_seconds;
    return Step(elapsed, sensitivity_x, sensitivity_y, invert_y, smoothing);
}
}  // namespace hulk
