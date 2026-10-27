#include "common/settings_ui.h"
#include <toml++/toml.hpp>
#include "common/camera.h"
#include "common/keyboard.h"

namespace hulk {

void LoadCustomSettings(const void* table_ptr) {
    if (!table_ptr) return;
    const auto& t = *static_cast<const toml::table*>(table_ptr);
    if (auto v = t["mouse_sensitivity"].value<double>()) {
        SetMouseSensitivity(static_cast<float>(*v));
    }
    if (auto v = t["mouse_invert_y"].value<bool>()) {
        SetMouseInvertY(*v);
    }
    if (auto v = t["mouse_smoothing"].value<bool>()) {
        SetMouseSmoothing(*v);
    }
    SetCameraInvert(t["camera_invert_x"].value_or(false), t["camera_invert_y"].value_or(false));
}

void SaveCustomSettings(void* table_ptr) {
    if (!table_ptr) return;
    auto& t = *static_cast<toml::table*>(table_ptr);
    t.insert_or_assign("mouse_sensitivity", GetMouseSensitivity());
    t.insert_or_assign("mouse_invert_y", GetMouseInvertY());
    t.insert_or_assign("mouse_smoothing", GetMouseSmoothing());
    t.insert_or_assign("camera_invert_x", GetCameraInvertX());  // Camera Options, as the player set them (camera.h)
    t.insert_or_assign("camera_invert_y", GetCameraInvertY());
}

}  // namespace hulk
