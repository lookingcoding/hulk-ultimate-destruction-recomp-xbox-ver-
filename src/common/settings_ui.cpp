#include "common/settings_ui.h"
#include <algorithm>
#include <string>
#include <vector>
#include <imgui.h>
#include <toml++/toml.hpp>
#include "app/app.h"
#include "common/keyboard.h"

namespace hulk {

namespace {

struct ResEntry {
    const char* label;
    uint32_t width;
    uint32_t height;
};

const ResEntry kStandardResolutions[] = {
    {"Desktop Native", 0, 0},
    {"3840 x 2160 (4K UHD 16:9)", 3840, 2160},
    {"2560 x 1440 (1440p QHD 16:9)", 2560, 1440},
    {"1920 x 1080 (1080p FHD 16:9)", 1920, 1080},
    {"1600 x 900 (900p 16:9)", 1600, 900},
    {"1280 x 720 (720p HD 16:9)", 1280, 720},
};

int FindCurrentResIndex(uint32_t w, uint32_t h) {
    for (size_t i = 0; i < std::size(kStandardResolutions); ++i) {
        if (kStandardResolutions[i].width == w && kStandardResolutions[i].height == h) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(std::size(kStandardResolutions));  // Custom
}

int FpsCapToIndex(uint32_t cap) {
    switch (cap) {
        case 0: return 0;   // Uncapped
        case 30: return 1;
        case 60: return 2;
        case 120: return 3;
        case 144: return 4;
        case 165: return 5;
        case 240: return 6;
        case app::kFpsCapRefresh: return 7;
        default: return 0;
    }
}

uint32_t IndexToFpsCap(int idx) {
    switch (idx) {
        case 0: return 0;
        case 1: return 30;
        case 2: return 60;
        case 3: return 120;
        case 4: return 144;
        case 5: return 165;
        case 6: return 240;
        case 7: return app::kFpsCapRefresh;
        default: return 60;
    }
}

int AnisoToIndex(uint32_t a) {
    switch (a) {
        case 0: return 0;
        case 2: return 1;
        case 4: return 2;
        case 8: return 3;
        case 16: return 4;
        default: return 0;
    }
}

uint32_t IndexToAniso(int idx) {
    switch (idx) {
        case 0: return 0;
        case 1: return 2;
        case 2: return 4;
        case 3: return 8;
        case 4: return 16;
        default: return 0;
    }
}

}  // namespace

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
}

void SaveCustomSettings(void* table_ptr) {
    if (!table_ptr) return;
    auto& t = *static_cast<toml::table*>(table_ptr);
    t.insert_or_assign("mouse_sensitivity", GetMouseSensitivity());
    t.insert_or_assign("mouse_invert_y", GetMouseInvertY());
    t.insert_or_assign("mouse_smoothing", GetMouseSmoothing());
}

void DrawSettingsUi(double fps) {
    const bool is_open = IsSettingsOpen();

    // When the menu is closed, show a subtle prompt in the top-right corner
    if (!is_open) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 badge_pos(io.DisplaySize.x - 10.0f, 10.0f);
        ImGui::SetNextWindowPos(badge_pos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.40f);
        ImGuiWindowFlags badge_flags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoFocusOnAppearing |
                                       ImGuiWindowFlags_NoNav |
                                       ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("##QuickPrompt", nullptr, badge_flags)) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "[F1] or [~] PC Graphics & Controls");
            ImGui::SameLine();
            ImGui::TextDisabled("| FPS: %.1f", fps);
        }
        ImGui::End();
        return;
    }

    // Main Settings Modal
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    bool open_state = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Hulk Recompiled - PC Settings & Graphics", &open_state, flags)) {
        app::Settings& s = app::Get();

        if (ImGui::BeginTabBar("SettingsTabs")) {
            // TAB 1: Display & Resolution
            if (ImGui::BeginTabItem("Display & Resolution")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Display Configuration");
                ImGui::Separator();

                // Display Mode
                int current_mode = static_cast<int>(s.display_mode);
                const char* display_modes[] = {"Exclusive Fullscreen", "Borderless Fullscreen", "Windowed"};
                if (ImGui::Combo("Display Mode", &current_mode, display_modes, IM_ARRAYSIZE(display_modes))) {
                    s.display_mode = static_cast<app::DisplayMode>(current_mode);
                }
                ImGui::TextDisabled("Borderless Fullscreen is recommended for seamless alt-tabbing on Windows 10/11.");

                ImGui::Spacing();
                ImGui::Separator();

                // Resolution
                int res_idx = FindCurrentResIndex(s.width, s.height);
                std::vector<std::string> res_labels;
                for (const auto& r : kStandardResolutions) {
                    if (r.width == 0) {
                        app::Mode d = app::Desktop();
                        res_labels.push_back("Desktop Native (" + std::to_string(d.width) + " x " + std::to_string(d.height) + ")");
                    } else {
                        res_labels.push_back(r.label);
                    }
                }
                res_labels.push_back("Custom Resolution");

                std::vector<const char*> res_ptrs;
                for (const auto& str : res_labels) res_ptrs.push_back(str.c_str());

                if (ImGui::Combo("Resolution Preset", &res_idx, res_ptrs.data(), static_cast<int>(res_ptrs.size()))) {
                    if (res_idx < static_cast<int>(std::size(kStandardResolutions))) {
                        s.width = kStandardResolutions[res_idx].width;
                        s.height = kStandardResolutions[res_idx].height;
                    }
                }

                if (res_idx == static_cast<int>(std::size(kStandardResolutions))) {
                    int w = static_cast<int>(s.width);
                    int h = static_cast<int>(s.height);
                    if (ImGui::InputInt("Custom Width", &w, 32, 128)) {
                        s.width = static_cast<uint32_t>(std::max(640, w));
                    }
                    if (ImGui::InputInt("Custom Height", &h, 32, 128)) {
                        s.height = static_cast<uint32_t>(std::max(480, h));
                    }
                }

                app::Mode current_out = app::Output();
                app::Mode desktop = app::Desktop();
                ImGui::Text("Current Output: %u x %u (Aspect %s) | Desktop: %u x %u",
                            current_out.width, current_out.height,
                            app::Widescreen() ? "16:9 Widescreen" : "4:3 Standard",
                            desktop.width, desktop.height);

                ImGui::Spacing();
                if (ImGui::Button("Apply Display Changes Now", ImVec2(220, 32))) {
                    app::ApplyDisplay();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(Applies resolution and fullscreen mode immediately)");

                ImGui::EndTabItem();
            }

            // TAB 2: Graphics & Rendering Quality
            if (ImGui::BeginTabItem("Graphics Quality")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Rendering & Performance");
                ImGui::Separator();

                // 3D Render Scale (Supersampling)
                int render_scale = static_cast<int>(s.render_percent);
                if (ImGui::SliderInt("3D Render Scale", &render_scale, 50, 200, "%d%%")) {
                    s.render_percent = static_cast<uint32_t>(render_scale);
                }
                ImGui::SameLine();
                if (ImGui::Button("100%")) s.render_percent = 100;
                ImGui::SameLine();
                if (ImGui::Button("150%")) s.render_percent = 150;
                ImGui::SameLine();
                if (ImGui::Button("200%")) s.render_percent = 200;

                ImGui::TextDisabled("Scales the internal 3D scene resolution. Values above 100%% provide pristine supersampled antialiasing.");

                ImGui::Spacing();
                ImGui::Separator();

                // VSync
                ImGui::Checkbox("Vertical Sync (VSync)", &s.vsync);
                ImGui::TextDisabled("Prevents screen tearing. Turn off for lowest input latency.");

                // FPS Cap
                int fps_idx = FpsCapToIndex(s.fps_cap);
                const char* fps_options[] = {
                    "Uncapped (Maximum)", "30 FPS", "60 FPS (Console Standard)", "120 FPS", "144 FPS", "165 FPS", "240 FPS", "Match Monitor Refresh Rate"
                };
                if (ImGui::Combo("Target Frame Rate Cap", &fps_idx, fps_options, IM_ARRAYSIZE(fps_options))) {
                    s.fps_cap = IndexToFpsCap(fps_idx);
                }

                // Anisotropic Filtering
                int aniso_idx = AnisoToIndex(s.aniso);
                const char* aniso_options[] = {"Off (Default)", "2x", "4x", "8x", "16x (Best Quality)"};
                if (ImGui::Combo("Anisotropic Texture Filtering", &aniso_idx, aniso_options, IM_ARRAYSIZE(aniso_options))) {
                    s.aniso = IndexToAniso(aniso_idx);
                }
                ImGui::TextDisabled("Sharpens textures viewed at oblique angles on pavement, walls, and roads.");

                ImGui::Spacing();
                if (ImGui::Button("Save Graphics Settings", ImVec2(200, 32))) {
                    app::Save();
                }

                ImGui::EndTabItem();
            }

            // TAB 3: Mouse & Camera Controls
            if (ImGui::BeginTabItem("Mouse & Camera")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Native PC Mouse Camera Overhaul");
                ImGui::Separator();

                float sens = GetMouseSensitivity();
                if (ImGui::SliderFloat("Mouse Look Sensitivity", &sens, 0.20f, 3.00f, "%.2fx")) {
                    SetMouseSensitivity(sens);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset##Sens")) SetMouseSensitivity(1.0f);

                bool invert_y = GetMouseInvertY();
                if (ImGui::Checkbox("Invert Vertical Look (Invert Y)", &invert_y)) {
                    SetMouseInvertY(invert_y);
                }

                bool smoothing = GetMouseSmoothing();
                if (ImGui::Checkbox("Mouse Smoothing (Deceleration Filter)", &smoothing)) {
                    SetMouseSmoothing(smoothing);
                }
                if (!smoothing) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "[Active: 1:1 Instant Stop]");
                    ImGui::TextDisabled("Camera stops rotating the exact microsecond your mouse stops moving. True PC feel.");
                } else {
                    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "[Active: Gentle Smoothing Filter]");
                    ImGui::TextDisabled("Slight 1-frame deceleration filter enabled.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Features:");
                ImGui::BulletText("Windows Raw Input: 100%% unaccelerated, high-resolution mouse sensor input.");
                ImGui::BulletText("Dynamic Deadzone Blending: Smooth micro-aiming without jerky threshold jumps.");
                ImGui::BulletText("Automatic Cursor Lock: Mouse stays locked in gameplay and is freed in menus.");

                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();

        ImGui::Spacing();
        ImGui::Separator();

        // Footer Action Bar
        if (ImGui::Button("Save & Close (F1)", ImVec2(160, 34))) {
            app::ApplyDisplay();
            app::Save();
            SetSettingsOpen(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close (Esc)", ImVec2(120, 34))) {
            SetSettingsOpen(false);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Press F1 or [~] to toggle this menu anytime.");
    }
    ImGui::End();

    if (!open_state) {
        SetSettingsOpen(false);
    }
}

}  // namespace hulk
