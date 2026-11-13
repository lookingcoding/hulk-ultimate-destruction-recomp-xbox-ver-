#include "common/settings_ui.h"
#include <algorithm>
#include <string>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <toml++/toml.hpp>
#include "common/camera.h"
#include "common/cheats.h"
#include "common/display_menu.h"
#include "common/keyboard.h"
#include "common/language.h"
#include "common/view.h"

namespace hulk {
namespace {
// settings.toml's renderer: "auto" starts Direct3D 12 and falls back to 11, "d3d11" starts 11 (the runtime reads
// XBE_RENDERER when it makes the renderer, after the settings load)
std::string g_renderer = "auto";
}  // namespace

void LoadCustomSettings(const void* table_ptr) {
    if (!table_ptr) return;
    const auto& t = *static_cast<const toml::table*>(table_ptr);
    // Options -> Cheats (cheats.h): a toggle as a bool, a cheat with more levels as its level; missing is off
    for (size_t i = 0; i < cheats::kCheatCount; i++) {
        const cheats::CheatInfo& info = cheats::Info(cheats::Cheat(i));
        int level = 0;
        if (auto b = t[info.key].value_exact<bool>()) level = *b ? 1 : 0;
        else if (auto n = t[info.key].value_exact<int64_t>()) level = int(std::clamp<int64_t>(*n, 0, info.levels - 1));
        cheats::Load(info.cheat, level);
    }
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
        else if (*v == "playstation") device = PromptDevice::Playstation;
        SetPromptDevice(device);
    }
    // field of view: degrees added to the game's; render distance: percent of the game's far and fog planes (view.h)
    if (auto v = t["fov_increase"].value<int64_t>()) SetFovIncrease(int(std::clamp<int64_t>(*v, 0, kMaxFovIncrease)));
    if (auto v = t["render_distance"].value<int64_t>())
        SetRenderDistance(int(std::clamp<int64_t>(*v, kMinRenderDistance, kMaxRenderDistance)));
    // camera distance: percent of the chase camera's (view.h); language: the game's, from the next start (language.h)
    if (auto v = t["camera_distance"].value<int64_t>())
        SetCameraDistance(int(std::clamp<int64_t>(*v, kMinCameraDistance, kMaxCameraDistance)));
    if (auto v = t["renderer"].value<std::string>()) {
        g_renderer = *v == "d3d11" ? "d3d11" : "auto";
        if (g_renderer == "d3d11") SetEnvironmentVariableW(L"XBE_RENDERER", L"d3d11");
    }
    if (auto v = t["language"].value<std::string>()) {
        Language language;
        if (LanguageFromName(v->c_str(), language)) SetLanguage(language);
    }
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
    t.insert_or_assign("camera_distance", int64_t(GetCameraDistance()));
    t.insert_or_assign("language", std::string(LanguageName(GetLanguage())));
    t.insert_or_assign("renderer", g_renderer);
    for (size_t i = 0; i < cheats::kCheatCount; i++) {
        const cheats::CheatInfo& info = cheats::Info(cheats::Cheat(i));
        const int level = cheats::Level(info.cheat);
        if (info.levels == 2) t.insert_or_assign(info.key, level != 0);
        else t.insert_or_assign(info.key, int64_t(level));
    }
}

}  // namespace hulk
