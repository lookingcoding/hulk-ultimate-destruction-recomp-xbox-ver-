#include "common/view.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>

namespace hulk {
namespace {
std::atomic<int> fov_increase{0};
std::atomic<int> render_distance{100};
std::atomic<uint32_t> generation{0};
std::atomic<int> camera_distance{100};
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
    constexpr int n = int(std::size(kCameraDistances));
    // the choice at or past the current distance in the direction of the step
    int i = 0;
    while (i < n && kCameraDistances[i] < now) i++;
    if (steps > 0 && i < n && kCameraDistances[i] == now) i++;
    else if (steps < 0) i--;
    i += steps > 0 ? steps - 1 : steps + 1;
    if (i < 0 || i >= n) return false;  // past the nearest or the farthest choice
    if (kCameraDistances[i] == now) return false;
    camera_distance.store(kCameraDistances[i]);
    return true;
}

float AdjustCameraDistance(float distance) {
    const int percent = camera_distance.load();
    if (percent == 100 || !std::isfinite(distance)) return distance;
    return distance * float(percent) / 100.0f;
}

}  // namespace hulk
