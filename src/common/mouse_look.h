#pragma once
#include <cstdint>

namespace hulk {
inline constexpr float kMouseSensitivityMin = 0.1f;
inline constexpr float kMouseSensitivityMax = 5.0f;
inline constexpr float kMouseSensitivityDefault = 1.0f;
float SanitizeMouseSensitivity(float value);

struct MouseStick { int16_t x = 0, y = 0; };

// Not internally synchronized: the window and polling threads share keyboard.cpp's mouse_mutex.
class MouseLook {
public:
    static constexpr double kSampleSeconds = 1.0 / 60.0;
    static constexpr double kCountsPerSecond = 1600.0;
    static constexpr int kDeadzone = 7500;
    void Reset();  // clear motion, retaining the selected input source
    void SetRawInputAvailable(bool available);
    void AddRaw(double dx, double dy);
    void AddFallback(double dx, double dy);
    // Advance on scheduled 60 Hz deadlines; intervening XInputGetState calls reuse the current sample.
    MouseStick Poll(double now_seconds, float sensitivity_x, float sensitivity_y, bool invert_y, bool smoothing);
    // For a future simulation hook: consume once per camera update with its actual dt, then cache the result.
    // Do not mix Step and Poll consumers. The native camera still determines angular speed/acceleration.
    MouseStick Step(double seconds, float sensitivity_x, float sensitivity_y, bool invert_y, bool smoothing);
private:
    void Add(double dx, double dy);
    double pending_x_ = 0, pending_y_ = 0;
    double last_poll_ = 0, next_sample_ = 0;
    bool sampled_ = false, raw_input_ = false;
    MouseStick cached_{};
};
}  // namespace hulk
