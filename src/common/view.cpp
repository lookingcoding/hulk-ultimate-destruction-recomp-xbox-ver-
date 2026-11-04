#include "common/view.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace hulk {
namespace {
std::atomic<int> fov_increase{0};
std::atomic<int> render_distance{100};
std::atomic<uint32_t> generation{0};
std::atomic<int> camera_distance{100};
std::atomic<float> camera_applied{0.0f};  // the scale the camera has now, 0 before its first update
constexpr float kRadiansPerDegree = 3.14159265358979f / 180.0f;
}  // namespace

void SetFovIncrease(int degrees) {
    degrees = std::clamp(degrees, 0, kMaxFovIncrease);
    if (fov_increase.exchange(degrees) != degrees) generation.fetch_add(1);
}
int GetFovIncrease() { return fov_increase.load(); }

void SetRenderDistance(int percent) {
    percent = std::clamp(percent, kMinRenderDistance, kMaxRenderDistance);
    if (render_distance.exchange(percent) != percent) generation.fetch_add(1);
}
int GetRenderDistance() { return render_distance.load(); }

uint32_t ViewGeneration() { return generation.load(); }

float AdjustFov(float radians) {
    const int add = fov_increase.load();
    if (!add || !std::isfinite(radians) || radians <= 0.0f) return radians;
    return std::max(radians, std::min(radians + float(add) * kRadiansPerDegree, kMaxFov * kRadiansPerDegree));
}

float AdjustDistance(float distance) {
    const int percent = render_distance.load();
    if (percent == 100 || !std::isfinite(distance)) return distance;
    return distance * float(percent) / 100.0f;
}

void SetCameraDistance(int percent) { camera_distance.store(std::clamp(percent, kMinCameraDistance, kMaxCameraDistance)); }
int GetCameraDistance() { return camera_distance.load(); }

bool StepCameraDistance(int steps) {
    const int now = camera_distance.load();
    float to = float(now) * std::pow(kCameraZoomStep, float(steps));
    int percent = std::clamp(int(std::lround(to)), kMinCameraDistance, kMaxCameraDistance);
    if (percent == now) percent = std::clamp(now + (steps > 0 ? 1 : -1), kMinCameraDistance, kMaxCameraDistance);
    if (percent == now) return false;
    camera_distance.store(percent);
    return true;
}

void TickCameraDistance(float seconds) {
    const float target = float(camera_distance.load()) / 100.0f;
    float now = camera_applied.load();
    if (now <= 0.0f || !std::isfinite(seconds) || seconds < 0.0f) {
        camera_applied.store(target);
        return;
    }
    // eased in log space, so zooming in and out feel the same; a long frame never overshoots
    const float t = 1.0f - std::exp(-kCameraZoomRate * std::min(seconds, 0.25f));
    now = std::exp(std::log(now) + (std::log(target) - std::log(now)) * t);
    if (std::fabs(now - target) < 0.001f) now = target;
    camera_applied.store(now);
}

float AdjustCameraDistance(float distance) {
    float scale = camera_applied.load();
    if (scale <= 0.0f) scale = float(camera_distance.load()) / 100.0f;
    if (scale == 1.0f || !std::isfinite(distance)) return distance;
    return distance * scale;
}

}  // namespace hulk
