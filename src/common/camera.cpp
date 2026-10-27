#include "common/camera.h"
#include <atomic>
#include "app/app.h"
#include "common/addresses.h"
#include "core/log.h"
#include "kernel/memory.h"

namespace hulk {
namespace {
std::atomic<bool> invert_x{false}, invert_y{false};
}  // namespace

void SetCameraInvert(bool x, bool y) {
    invert_x.store(x);
    invert_y.store(y);
}
bool GetCameraInvertX() { return invert_x.load(); }
bool GetCameraInvertY() { return invert_y.load(); }

void CameraInvertChanged() {
    const uint8_t* base = mem::Base();
    if (!base) return;
    const bool x = !base[addr::kCameraInvertX], y = !base[addr::kCameraInvertY];
    if (x == invert_x.load() && y == invert_y.load()) return;
    SetCameraInvert(x, y);
    LOGI("camera: invert x %d, y %d", x, y);
    app::Save();
}

void KeepCameraInvert() {
    uint8_t* base = mem::Base();
    if (!base) return;
    base[addr::kCameraInvertX] = invert_x.load() ? 0 : 1;
    base[addr::kCameraInvertY] = invert_y.load() ? 0 : 1;
}

}  // namespace hulk
