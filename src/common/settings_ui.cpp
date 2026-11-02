#include "common/settings_ui.h"
#include <algorithm>
#include <toml++/toml.hpp>
#include "common/camera.h"
#include "common/display_menu.h"
#include "common/keyboard.h"
#include "common/view.h"

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
    // whose button prompts to show: a controller's when one is connected, the keyboard's, or one of them always
    if (auto v = t["button_prompts"].value<std::string>()) {
        PromptDevice device = PromptDevice::Auto;
        if (*v == "keyboard") device = PromptDevice::Keyboard;
        else if (*v == "controller") device = PromptDevice::Controller;
        SetPromptDevice(device);
    }
    // field of view: degrees added to the game's; render distance: percent of the game's far and fog planes (view.h)
    if (auto v = t["fov_increase"].value<int64_t>()) SetFovIncrease(int(std::clamp<int64_t>(*v, 0, kMaxFovIncrease)));
    if (auto v = t["render_distance"].value<int64_t>())
        SetRenderDistance(int(std::clamp<int64_t>(*v, kMinRenderDistance, kMaxRenderDistance)));
}

void SaveCustomSettings(void* table_ptr) {
    if (!table_ptr) return;
    auto& t = *static_cast<toml::table*>(table_ptr);
    t.insert_or_assign("mouse_sensitivity", GetMouseSensitivity());
    t.insert_or_assign("mouse_invert_y", GetMouseInvertY());
    t.insert_or_assign("mouse_smoothing", GetMouseSmoothing());
    t.insert_or_assign("camera_invert_x", GetCameraInvertX());  // Camera Options, as the player set them (camera.h)
    t.insert_or_assign("camera_invert_y", GetCameraInvertY());
    t.insert_or_assign("button_prompts", std::string(PromptDeviceName(GetPromptDevice())));
    t.insert_or_assign("fov_increase", int64_t(GetFovIncrease()));
    t.insert_or_assign("render_distance", int64_t(GetRenderDistance()));
}

}  // namespace hulk
