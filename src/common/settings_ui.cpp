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
    // Check if the Options menu should be active
    const bool is_open = IsSettingsOpen();
    if (!is_open) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();

    // Full-screen Options menu presentation matching the native game experience
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags bg_flags = ImGuiWindowFlags_NoDecoration |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.07f, 0.09f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.35f, 0.20f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.50f, 0.28f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.65f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.38f, 0.22f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.55f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.72f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.10f, 0.24f, 0.16f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.18f, 0.50f, 0.30f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.16f, 0.46f, 0.26f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 4.0f);

    if (ImGui::Begin("##GameOptionsMenu", nullptr, bg_flags)) {
        app::Settings& s = app::Get();

        // Header Title Banner
        ImGui::TextColored(ImVec4(0.85f, 0.90f, 0.88f, 1.0f), "THE INCREDIBLE HULK: ULTIMATE DESTRUCTION");
        ImGui::SameLine();
        ImGui::TextDisabled("| Performance: %.1f FPS", fps);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.24f, 0.88f, 0.48f, 1.0f));
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Text("OPTIONS & PC SETTINGS");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("OptionsTabs", ImGuiTabBarFlags_None)) {
            // TAB 1: Display & Resolution
            if (ImGui::BeginTabItem("DISPLAY & RESOLUTION")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.24f, 0.88f, 0.48f, 1.0f), "Display Configuration");
                ImGui::Separator();

                // Display Mode
                int current_mode = static_cast<int>(s.display_mode);
                const char* display_modes[] = {"Exclusive Fullscreen", "Borderless Fullscreen", "Windowed"};
                if (ImGui::Combo("Display Mode", &current_mode, display_modes, IM_ARRAYSIZE(display_modes))) {
                    s.display_mode = static_cast<app::DisplayMode>(current_mode);
                }
                ImGui::TextDisabled("Borderless Fullscreen allows smooth, instant alt-tabbing to desktop without display mode flicker.");

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
                ImGui::Text("Current Output: %u x %u (Aspect %s) | Desktop Native: %u x %u",
                            current_out.width, current_out.height,
                            app::Widescreen() ? "16:9 Widescreen" : "4:3 Standard",
                            desktop.width, desktop.height);

                ImGui::Spacing();
                if (ImGui::Button("Apply Display Changes Now", ImVec2(240, 36))) {
                    app::ApplyDisplay();
                }

                ImGui::EndTabItem();
            }

            // TAB 2: Graphics Quality
            if (ImGui::BeginTabItem("GRAPHICS QUALITY")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.24f, 0.88f, 0.48f, 1.0f), "Rendering & Enhancement");
                ImGui::Separator();

                // 3D Render Scale (Supersampling)
                int render_scale = static_cast<int>(s.render_percent);
                if (ImGui::SliderInt("3D Render Scale", &render_scale, 50, 200, "%d%%")) {
                    s.render_percent = static_cast<uint32_t>(render_scale);
                }
                ImGui::SameLine();
                if (ImGui::Button("100% Native")) s.render_percent = 100;
                ImGui::SameLine();
                if (ImGui::Button("150% Super")) s.render_percent = 150;
                ImGui::SameLine();
                if (ImGui::Button("200% Ultra 4K")) s.render_percent = 200;

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
                ImGui::TextDisabled("Sharpens textures viewed at oblique angles on streets, walls, and distant buildings.");

                ImGui::EndTabItem();
            }

            // TAB 3: Mouse & Controls
            if (ImGui::BeginTabItem("MOUSE & CONTROLS")) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.24f, 0.88f, 0.48f, 1.0f), "Mouse Input & Camera Control");
                ImGui::Separator();

                float sens = GetMouseSensitivity();
                if (ImGui::SliderFloat("Mouse Look Sensitivity", &sens, 0.20f, 3.00f, "%.2fx")) {
                    SetMouseSensitivity(sens);
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset to Default")) SetMouseSensitivity(1.0f);

                bool invert_y = GetMouseInvertY();
                if (ImGui::Checkbox("Invert Vertical Look (Invert Y)", &invert_y)) {
                    SetMouseInvertY(invert_y);
                }

                bool smoothing = GetMouseSmoothing();
                if (ImGui::Checkbox("Mouse Smoothing (Deceleration Filter)", &smoothing)) {
                    SetMouseSmoothing(smoothing);
                }
                if (!smoothing) {
                    ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "[Active: 1:1 Instant Stop - True PC Native]");
                    ImGui::TextDisabled("Camera stops rotating the exact microsecond your mouse stops moving. Unfiltered 1:1 response.");
                } else {
                    ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.35f, 1.0f), "[Active: Gentle Smoothing Filter]");
                    ImGui::TextDisabled("Slight 1-frame deceleration filter enabled.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.85f, 0.90f, 0.88f, 1.0f), "Mouse Integration Status:");
                ImGui::BulletText("Windows Raw Input: 100%% direct, unaccelerated mouse sensor input.");
                ImGui::BulletText("Menu Direct Hit-Testing: Mouse clicks and highlights align directly with menu items.");
                ImGui::BulletText("Automatic Cursor Lock: Mouse is captured during gameplay and freed in menus.");

                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Footer Action Bar
        if (ImGui::Button("SAVE & APPLY", ImVec2(180, 38))) {
            app::ApplyDisplay();
            app::Save();
            SetSettingsOpen(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("BACK TO GAME / MENU", ImVec2(200, 38))) {
            app::Save();
            SetSettingsOpen(false);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Press [ESC] or Right-Click anytime to return to previous menu.");
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(10);
}

}  // namespace hulk
