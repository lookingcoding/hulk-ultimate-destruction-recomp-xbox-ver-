#include "common/settings_ui.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>
#include <imgui.h>
#include <toml++/toml.hpp>
#include "app/app.h"
#include "common/keyboard.h"

namespace hulk {

namespace {

std::atomic<bool> g_display_options_open{false};
std::atomic<int> g_selected_row{0};
std::atomic<int> g_hovered_row{-1};

struct ResolutionPreset {
    uint32_t width;
    uint32_t height;
    const char* label;
};

const ResolutionPreset kResolutions[] = {
    {1280, 720, "1280 X 720 (720P HD)"},
    {1600, 900, "1600 X 900 (HD+)"},
    {1920, 1080, "1920 X 1080 (1080P FHD)"},
    {2560, 1440, "2560 X 1440 (1440P QHD)"},
    {3840, 2160, "3840 X 2160 (4K UHD)"},
};

const uint32_t kFpsCaps[] = {30, 60, 120, 144, 0};
const char* kFpsLabels[] = {"30 FPS", "60 FPS", "120 FPS", "144 FPS", "UNCAPPED"};

const uint32_t kRenderPercents[] = {100, 125, 150, 200};
const char* kRenderLabels[] = {"100% (NATIVE)", "125% (HIGH)", "150% (SUPER)", "200% (ULTRA 4K)"};

int FindResolutionIndex(uint32_t w, uint32_t h) {
    for (size_t i = 0; i < std::size(kResolutions); ++i) {
        if (kResolutions[i].width == w && kResolutions[i].height == h) {
            return static_cast<int>(i);
        }
    }
    return 2;  // Default to 1080p
}

int FindFpsIndex(uint32_t cap) {
    for (size_t i = 0; i < std::size(kFpsCaps); ++i) {
        if (kFpsCaps[i] == cap) return static_cast<int>(i);
    }
    return 1;  // 60 FPS
}

int FindRenderIndex(uint32_t percent) {
    for (size_t i = 0; i < std::size(kRenderPercents); ++i) {
        if (kRenderPercents[i] == percent) return static_cast<int>(i);
    }
    return 0;  // 100%
}

void CycleOption(int row, int direction) {
    auto& s = app::Get();
    switch (row) {
        case 0: {  // Display Mode
            int mode = static_cast<int>(s.display_mode);
            mode = (mode + direction + 3) % 3;
            s.display_mode = static_cast<app::DisplayMode>(mode);
            app::ApplyDisplay();
            app::Save();
            break;
        }
        case 1: {  // Resolution
            int idx = FindResolutionIndex(s.width, s.height);
            int count = static_cast<int>(std::size(kResolutions));
            idx = (idx + direction + count) % count;
            s.width = kResolutions[idx].width;
            s.height = kResolutions[idx].height;
            app::ApplyDisplay();
            app::Save();
            break;
        }
        case 2: {  // Frame Rate Cap
            int idx = FindFpsIndex(s.fps_cap);
            int count = static_cast<int>(std::size(kFpsCaps));
            idx = (idx + direction + count) % count;
            s.fps_cap = kFpsCaps[idx];
            app::Save();
            break;
        }
        case 3: {  // Vertical Sync
            s.vsync = !s.vsync;
            app::ApplyDisplay();
            app::Save();
            break;
        }
        case 4: {  // 3D Render Scale
            int idx = FindRenderIndex(s.render_percent);
            int count = static_cast<int>(std::size(kRenderPercents));
            idx = (idx + direction + count) % count;
            s.render_percent = kRenderPercents[idx];
            app::ApplyDisplay();
            app::Save();
            break;
        }
        default:
            break;
    }
}

// Draws the 1:1 Radical FE glowing neon green highlight bar across the screen
void DrawNativeSelectBar(ImDrawList* draw, float y_top, float y_bot, float screen_w) {
    const float h = y_bot - y_top;
    const float y_mid = y_top + h * 0.45f;

    // Top soft green edge
    draw->AddRectFilledMultiColor(
        ImVec2(0.0f, y_top), ImVec2(screen_w, y_mid),
        IM_COL32(35, 175, 45, 160), IM_COL32(35, 175, 45, 160),
        IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255)
    );

    // Bottom vivid neon green edge
    draw->AddRectFilledMultiColor(
        ImVec2(0.0f, y_mid), ImVec2(screen_w, y_bot),
        IM_COL32(255, 255, 255, 255), IM_COL32(255, 255, 255, 255),
        IM_COL32(30, 160, 40, 200), IM_COL32(30, 160, 40, 200)
    );

    // Subtle dark boundary lines for crisp console definition
    draw->AddLine(ImVec2(0.0f, y_top), ImVec2(screen_w, y_top), IM_COL32(20, 110, 30, 255), 1.5f);
    draw->AddLine(ImVec2(0.0f, y_bot), ImVec2(screen_w, y_bot), IM_COL32(10, 70, 20, 255), 1.5f);
}

// Draws the 1:1 Radical FE dark translucent background bar
void DrawNativeBackgroundBar(ImDrawList* draw, float y_top, float y_bot, float screen_w) {
    draw->AddRectFilled(
        ImVec2(0.0f, y_top), ImVec2(screen_w, y_bot),
        IM_COL32(10, 14, 12, 170)
    );
    // 1px bevel highlights
    draw->AddLine(ImVec2(0.0f, y_top), ImVec2(screen_w, y_top), IM_COL32(75, 80, 78, 180), 1.0f);
    draw->AddLine(ImVec2(0.0f, y_bot), ImVec2(screen_w, y_bot), IM_COL32(5, 8, 6, 220), 1.0f);
}

// Draws the iconic angular trapezoid header banner matching the game's Serpentine styling
void DrawNativeHeader(ImDrawList* draw, const char* title, float screen_w, float screen_h) {
    const float cx = screen_w * 0.5f;
    const float bw = screen_w * 0.52f;
    const float y0 = (38.0f / 480.0f) * screen_h;
    const float y1 = (92.0f / 480.0f) * screen_h;
    const float chamfer = (18.0f / 480.0f) * screen_h;

    ImVec2 pts[6] = {
        ImVec2(cx - bw * 0.5f + chamfer, y0),
        ImVec2(cx + bw * 0.5f - chamfer, y0),
        ImVec2(cx + bw * 0.5f, y0 + chamfer),
        ImVec2(cx + bw * 0.5f, y1),
        ImVec2(cx - bw * 0.5f, y1),
        ImVec2(cx - bw * 0.5f, y0 + chamfer),
    };

    draw->AddConvexPolyFilled(pts, 6, IM_COL32(12, 16, 14, 240));
    draw->AddPolyline(pts, 6, IM_COL32(170, 180, 175, 255), ImDrawFlags_Closed, 1.8f);

    // Title text: centered, bold mint white
    ImVec2 tsz = ImGui::CalcTextSize(title);
    float tx = cx - tsz.x * 0.5f;
    float ty = y0 + (y1 - y0 - tsz.y) * 0.5f;

    // Drop shadow
    draw->AddText(ImVec2(tx + 2.0f, ty + 2.0f), IM_COL32(0, 0, 0, 220), title);
    draw->AddText(ImVec2(tx, ty), IM_COL32(230, 255, 230, 255), title);
}

// Draws the iconic bottom console button prompt bar
void DrawNativeFooter(ImDrawList* draw, float screen_w, float screen_h) {
    const float cx = screen_w * 0.5f;
    const float bw = screen_w * 0.48f;
    const float y0 = (440.0f / 480.0f) * screen_h;
    const float y1 = (476.0f / 480.0f) * screen_h;
    const float chamfer = (14.0f / 480.0f) * screen_h;

    ImVec2 pts[6] = {
        ImVec2(cx - bw * 0.5f, y0),
        ImVec2(cx + bw * 0.5f, y0),
        ImVec2(cx + bw * 0.5f, y1 - chamfer),
        ImVec2(cx + bw * 0.5f - chamfer, y1),
        ImVec2(cx - bw * 0.5f + chamfer, y1),
        ImVec2(cx - bw * 0.5f, y1 - chamfer),
    };

    draw->AddConvexPolyFilled(pts, 6, IM_COL32(12, 16, 14, 240));
    draw->AddPolyline(pts, 6, IM_COL32(170, 180, 175, 255), ImDrawFlags_Closed, 1.8f);

    // [A] Change / Select    [B] Back
    const float btn_radius = 8.0f * (screen_h / 480.0f);
    const float cy = (y0 + y1) * 0.5f;

    // Button A (Green)
    const float a_x = cx - 120.0f * (screen_w / 640.0f);
    draw->AddCircleFilled(ImVec2(a_x, cy), btn_radius, IM_COL32(40, 180, 50, 255));
    draw->AddCircle(ImVec2(a_x, cy), btn_radius, IM_COL32(220, 255, 220, 255), 0, 1.5f);
    draw->AddText(ImVec2(a_x - 4.0f, cy - 7.0f), IM_COL32(0, 0, 0, 255), "A");
    draw->AddText(ImVec2(a_x + btn_radius + 6.0f, cy - 7.0f), IM_COL32(235, 240, 235, 255), "Select / Change");

    // Button B (Red)
    const float b_x = cx + 45.0f * (screen_w / 640.0f);
    draw->AddCircleFilled(ImVec2(b_x, cy), btn_radius, IM_COL32(220, 45, 40, 255));
    draw->AddCircle(ImVec2(b_x, cy), btn_radius, IM_COL32(255, 220, 220, 255), 0, 1.5f);
    draw->AddText(ImVec2(b_x - 4.0f, cy - 7.0f), IM_COL32(0, 0, 0, 255), "B");
    draw->AddText(ImVec2(b_x + btn_radius + 6.0f, cy - 7.0f), IM_COL32(235, 240, 235, 255), "Back");
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

bool IsDisplayOptionsOpen() {
    return g_display_options_open.load(std::memory_order_relaxed);
}

void SetDisplayOptionsOpen(bool open) {
    g_display_options_open.store(open, std::memory_order_relaxed);
    if (open) {
        g_selected_row.store(0, std::memory_order_relaxed);
        g_hovered_row.store(-1, std::memory_order_relaxed);
    }
}

void DrawNativeDisplayUi(double fps) {
    (void)fps;
    ImGuiIO& io = ImGui::GetIO();
    const float screen_w = io.DisplaySize.x;
    const float screen_h = io.DisplaySize.y;
    if (screen_w <= 0.0f || screen_h <= 0.0f) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw) return;

    const bool in_menu = IsInMenuState();
    const bool is_options = IsCurrentScreenOptions();
    const bool is_sub_open = IsDisplayOptionsOpen();

    // 1. If currently inside the native Options menu, seamlessly render Row 3: "Display Options"
    if (in_menu && is_options && !is_sub_open) {
        constexpr float kStartY = 110.0f;
        constexpr float kRowH = 31.5f;
        const float y_top = ((kStartY + 3.0f * kRowH) / 480.0f) * screen_h;
        const float y_bot = ((kStartY + 4.0f * kRowH) / 480.0f) * screen_h;
        const float text_x = (88.0f / 640.0f) * screen_w;
        const float text_y = y_top + (y_bot - y_top) * 0.22f;

        const int current_sel = GetCurrentMenuSelectedRow();
        const bool is_sel = (current_sel == 3);

        if (is_sel) {
            DrawNativeSelectBar(draw, y_top, y_bot, screen_w);
            // Black text on neon green highlight bar with soft shadow
            draw->AddText(ImVec2(text_x + 1.5f, text_y + 1.5f), IM_COL32(0, 0, 0, 140), "Display Options");
            draw->AddText(ImVec2(text_x, text_y), IM_COL32(0, 0, 0, 255), "Display Options");
        } else {
            DrawNativeBackgroundBar(draw, y_top, y_bot, screen_w);
            // Crisp silver text on dark background
            draw->AddText(ImVec2(text_x + 1.5f, text_y + 1.5f), IM_COL32(0, 0, 0, 220), "Display Options");
            draw->AddText(ImVec2(text_x, text_y), IM_COL32(215, 215, 215, 255), "Display Options");
        }
        return;
    }

    // 2. If the Display Options sub-screen is open, render the complete 1:1 screen
    if (is_sub_open) {
        // Darkened full-screen blur/tint matching the native pause/menu backdrop
        draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(screen_w, screen_h), IM_COL32(0, 0, 0, 210));

        // Native Top Header: "DISPLAY OPTIONS"
        DrawNativeHeader(draw, "DISPLAY OPTIONS", screen_w, screen_h);

        // Options rows
        const auto& s = app::Get();
        const char* mode_labels[] = {"WINDOWED", "BORDERLESS", "FULLSCREEN"};
        const int mode_idx = std::clamp(static_cast<int>(s.display_mode), 0, 2);
        const int res_idx = FindResolutionIndex(s.width, s.height);
        const int fps_idx = FindFpsIndex(s.fps_cap);
        const int render_idx = FindRenderIndex(s.render_percent);

        struct OptionEntry {
            const char* label;
            std::string value;
        };

        OptionEntry entries[5] = {
            {"DISPLAY MODE", "< " + std::string(mode_labels[mode_idx]) + " >"},
            {"RESOLUTION", "< " + std::string(kResolutions[res_idx].label) + " >"},
            {"FRAME RATE CAP", "< " + std::string(kFpsLabels[fps_idx]) + " >"},
            {"VERTICAL SYNC", s.vsync ? "< ON >" : "< OFF >"},
            {"3D RENDER SCALE", "< " + std::string(kRenderLabels[render_idx]) + " >"},
        };

        constexpr float kStartY = 115.0f;
        constexpr float kRowH = 34.0f;
        const int sel = g_selected_row.load(std::memory_order_relaxed);

        for (int i = 0; i < 5; ++i) {
            const float y_top = ((kStartY + i * kRowH) / 480.0f) * screen_h;
            const float y_bot = ((kStartY + (i + 1) * kRowH) / 480.0f) * screen_h;
            const float text_x = (88.0f / 640.0f) * screen_w;
            const float text_y = y_top + (y_bot - y_top) * 0.22f;
            const float val_x = (360.0f / 640.0f) * screen_w;

            const bool is_active = (i == sel);

            if (is_active) {
                DrawNativeSelectBar(draw, y_top, y_bot, screen_w);
                // Crisp black text on neon green highlight
                draw->AddText(ImVec2(text_x + 1.5f, text_y + 1.5f), IM_COL32(0, 0, 0, 140), entries[i].label);
                draw->AddText(ImVec2(text_x, text_y), IM_COL32(0, 0, 0, 255), entries[i].label);

                draw->AddText(ImVec2(val_x + 1.5f, text_y + 1.5f), IM_COL32(0, 0, 0, 140), entries[i].value.c_str());
                draw->AddText(ImVec2(val_x, text_y), IM_COL32(0, 0, 0, 255), entries[i].value.c_str());
            } else {
                DrawNativeBackgroundBar(draw, y_top, y_bot, screen_w);
                // Silver label, white value
                draw->AddText(ImVec2(text_x + 1.5f, text_y + 1.5f), IM_COL32(0, 0, 0, 220), entries[i].label);
                draw->AddText(ImVec2(text_x, text_y), IM_COL32(215, 215, 215, 255), entries[i].label);

                draw->AddText(ImVec2(val_x + 1.5f, text_y + 1.5f), IM_COL32(0, 0, 0, 220), entries[i].value.c_str());
                draw->AddText(ImVec2(val_x, text_y), IM_COL32(250, 250, 250, 255), entries[i].value.c_str());
            }
        }

        // Native bottom button prompt bar
        DrawNativeFooter(draw, screen_w, screen_h);
    }
}

bool HandleDisplayInputKey(uint32_t vk, bool down) {
    if (!g_display_options_open.load(std::memory_order_relaxed)) return false;
    if (!down) return true;  // consume release

    switch (vk) {
        case VK_UP:
        case 'W': {
            int cur = g_selected_row.load(std::memory_order_relaxed);
            if (cur > 0) g_selected_row.store(cur - 1, std::memory_order_relaxed);
            return true;
        }
        case VK_DOWN:
        case 'S': {
            int cur = g_selected_row.load(std::memory_order_relaxed);
            if (cur < 4) g_selected_row.store(cur + 1, std::memory_order_relaxed);
            return true;
        }
        case VK_LEFT:
        case 'A': {
            int cur = g_selected_row.load(std::memory_order_relaxed);
            CycleOption(cur, -1);
            return true;
        }
        case VK_RIGHT:
        case 'D':
        case VK_SPACE:
        case VK_RETURN: {
            int cur = g_selected_row.load(std::memory_order_relaxed);
            CycleOption(cur, 1);
            return true;
        }
        case VK_ESCAPE:
        case 'Q':
        case VK_BACK: {
            // Save and return to the native Options menu
            app::ApplyDisplay();
            app::Save();
            SetDisplayOptionsOpen(false);
            SetCurrentMenuSelectedRow(3);
            return true;
        }
        default:
            return true;
    }
}

bool HandleDisplayClick(int cursor_x, int cursor_y, int client_w, int client_h, bool right_click) {
    if (!g_display_options_open.load(std::memory_order_relaxed)) return false;

    if (right_click) {
        // Right click = Button B (Back)
        app::ApplyDisplay();
        app::Save();
        SetDisplayOptionsOpen(false);
        SetCurrentMenuSelectedRow(3);
        return true;
    }

    if (client_w <= 0 || client_h <= 0) return true;
    const float vy = (static_cast<float>(cursor_y) / static_cast<float>(client_h)) * 480.0f;
    constexpr float kStartY = 115.0f;
    constexpr float kRowH = 34.0f;

    if (vy >= kStartY && vy < kStartY + 5.0f * kRowH) {
        int row = static_cast<int>((vy - kStartY) / kRowH);
        row = std::clamp(row, 0, 4);
        g_selected_row.store(row, std::memory_order_relaxed);
        // Cycle the option forward on click
        CycleOption(row, 1);
        return true;
    }

    return true;
}

void HandleDisplayWheel(int delta) {
    if (!g_display_options_open.load(std::memory_order_relaxed)) return;
    int cur = g_selected_row.load(std::memory_order_relaxed);
    CycleOption(cur, delta > 0 ? 1 : -1);
}

void HandleDisplayHover(int cursor_x, int cursor_y, int client_w, int client_h) {
    if (!g_display_options_open.load(std::memory_order_relaxed)) return;
    if (client_w <= 0 || client_h <= 0) return;
    const float vy = (static_cast<float>(cursor_y) / static_cast<float>(client_h)) * 480.0f;
    constexpr float kStartY = 115.0f;
    constexpr float kRowH = 34.0f;

    if (vy >= kStartY && vy < kStartY + 5.0f * kRowH) {
        int row = static_cast<int>((vy - kStartY) / kRowH);
        row = std::clamp(row, 0, 4);
        g_selected_row.store(row, std::memory_order_relaxed);
    }
}

}  // namespace hulk
