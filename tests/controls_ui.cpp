#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <imgui.h>
#include <imgui_internal.h>
#include "common/bindings.h"
#include "common/controls_ui.h"
#include "core/input.h"
#include "controls_capture.h"

namespace {
std::unordered_map<ImGuiID, ImRect> rectangles;
std::unordered_map<std::string, ImRect> items;
bool requested = false;
int changes = 0, saves = 0, failures = 0;
float sensitivity_x = 1, sensitivity_y = 1;
bool invert_y = false;
std::filesystem::path capture_dir;
void Check(bool value, const char* what) {
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); }
    else if (!capture_dir.empty()) std::printf("PASS: %s\n", what);
}
void Capture(const char* name) {
    if (!capture_dir.empty()) {
        const auto* window = ImGui::FindWindowByName("Controls / Keybinds");
        if (!window) throw std::runtime_error("Controls window missing from capture");
        CaptureControlsPng(capture_dir / name, window->Pos, window->Size);
    }
}
void Frame() {
    items.clear(); rectangles.clear();
    ImGui::NewFrame();
    hulk::DrawControls(60);
    ImGui::Render();
}
ImVec2 Position(const char* prefix) {
    if (items.contains(prefix)) return items.at(prefix).GetCenter();
    for (const auto& [label, rectangle] : items)
        if (label.starts_with(prefix)) return rectangle.GetCenter();
    Check(false, prefix);
    return {};
}
void Click(ImVec2 position) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(position.x, position.y);
    Frame();
    io.AddMouseButtonEvent(0, true);
    hulk::ControlsMessage(WM_LBUTTONDOWN, 0, 0, nullptr);
    Frame();
    io.AddMouseButtonEvent(0, false);
    hulk::ControlsMessage(WM_LBUTTONUP, 0, 0, nullptr);
    Frame();
}
}
void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& rect, const ImGuiLastItemData*) { rectangles[id] = rect; }
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    if (label && rectangles.contains(id)) items[label] = rectangles.at(id);
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return "headless-controls-test"; }
namespace hulk {
bool TakeControlsRequest() { const bool value = requested; requested = false; return value; }
void NoteSettingChanged() { ++changes; }
}
namespace app { void Save() { ++saves; } }

int Run(int argc, char** argv) {
    if (argc != 1 && argc != 4) {
        std::fprintf(stderr, "Usage: hulk_controls_tests [capture-directory width height]\n");
        return 2;
    }
    if (argc == 4) {
        capture_dir = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(capture_dir);
    }
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = argc == 4 ? ImVec2(float(std::stoi(argv[2])), float(std::stoi(argv[3]))) : ImVec2(1280, 900);
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
    hulk::SetControlsMouseHooks({[] { return sensitivity_x; }, [](float v) { sensitivity_x = v; },
        [] { return sensitivity_y; }, [](float v) { sensitivity_y = v; },
        [] { return invert_y; }, [](bool v) { invert_y = v; }});
    requested = true;
    Frame(); Frame();
    Check(hulk::ControlsOpen() && ImGui::GetDrawData()->TotalVtxCount > 0, "native menu request opens a rendered Controls panel");
    Check(items.contains("Mouse sensitivity X") && items.contains("Mouse sensitivity Y") && items.contains("Invert mouse Y"),
        "both sensitivity sliders and inversion render in Controls");
    Capture("01-default.png");
    ImGuiWindow* actions = nullptr;
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (std::string(window->Name).find("/Actions_") != std::string::npos) actions = window;
    Check(actions != nullptr, "action list has its own scrollable region");
    if (!actions) return 1;
    const auto scroll = [&](float wheel) {
        io.AddMousePosEvent(actions->InnerRect.GetCenter().x, actions->InnerRect.GetCenter().y);
        io.AddMouseWheelEvent(0, wheel);
        Frame(); Frame();
    };
    scroll(-40);
    Check(actions->InnerClipRect.Contains(Position("Camera farther")), "last action is reachable by scrolling");
    Capture("00-menu-and-zoom-actions.png");
    scroll(40);
    Click(Position("Mouse sensitivity X"));
    Check(sensitivity_x != 1 && sensitivity_y == 1, "X slider changes X independently");
    const float previous_x = sensitivity_x;
    Click(Position("Mouse sensitivity Y"));
    Check(sensitivity_y != 1 && sensitivity_x == previous_x, "Y slider changes Y independently");
    Click(Position("Invert mouse Y"));
    Check(invert_y, "invert Y checkbox changes the setting");
    changes = 0;
    input::Pad pad{}; pad.connected = true; pad.buttons = 8; pad.analog[0] = 255;
    Check(hulk::ControlsPad(0, pad) && pad.connected && !pad.buttons && !pad.analog[0], "Controls suppresses gamepad reports");
    Click(Position("Move forward"));
    Capture("02-listening.png");
    hulk::ControlsMessage(WM_KEYDOWN, 'K', 0, nullptr);
    Frame();
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{'K'} && changes == 1,
        "selecting a row and pressing a key updates its binding");
    Frame();
    Capture("03-rebound.png");
    Click(Position("Move forward"));
    Click(Position("Cancel capture"));
    hulk::ControlsMessage(WM_KEYDOWN, 'L', 0, nullptr);
    Frame();
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{'K'}, "Cancel capture does not bind its mouse click");
    Click(Position("Move forward"));
    hulk::ControlsMessage(WM_MBUTTONDOWN, 0, 0, nullptr);
    Frame();
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{VK_MBUTTON}, "row capture accepts a mouse button");
    Click(Position("Move forward"));
    hulk::ControlsMessage(WM_KEYDOWN, 'S', 0, nullptr);
    Frame(); Frame();
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{VK_MBUTTON}, "conflicting binding waits for a decision");
    Capture("04-conflict.png");
    Click(Position("Cancel"));
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{VK_MBUTTON}, "Cancel conflict preserves the current binding");
    Click(Position("Move forward"));
    hulk::ControlsMessage(WM_KEYDOWN, 'S', 0, nullptr);
    Frame(); Frame();
    Click(Position("Replace conflicting bindings"));
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{'S'} && hulk::bindings::Get()->inputs[1].empty(),
        "conflict replacement removes the conflicting action binding");
    Click(Position("Move backward"));
    hulk::ControlsMessage(WM_KEYDOWN, 'S', 0, nullptr);
    Frame(); Frame();
    Click(Position("Keep both"));
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{'S'} &&
        hulk::bindings::Get()->inputs[1] == std::vector<hulk::bindings::Input>{'S'}, "Keep both explicitly allows a shared binding");
    sensitivity_x = 3; sensitivity_y = 2; invert_y = true;
    Click(Position("Reset to defaults")); Frame();
    Capture("05-reset-confirmation.png");
    Click(Position("Reset"));
    Check(hulk::bindings::Get()->inputs == hulk::bindings::Defaults().inputs && sensitivity_x == 1 && sensitivity_y == 1 && !invert_y,
        "Reset restores bindings and all mouse settings");
    Frame();
    Capture("06-restored.png");
    Click(Position("Save and close"));
    Check(!hulk::ControlsOpen() && saves == 1, "Save and close persists settings and closes Controls");
    Check(!hulk::ControlsMessage(WM_KEYDOWN, 'K', 0, nullptr), "closed Controls passes input through");
    Frame();
    io.AddMousePosEvent(-100, -100);
    requested = true;
    Frame(); Frame();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    hulk::ControlsMessage(WM_KEYDOWN, VK_RETURN, 0, nullptr);
    Frame();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    hulk::ControlsMessage(WM_KEYUP, VK_RETURN, 0, nullptr);
    Frame();
    hulk::ControlsMessage(WM_KEYDOWN, 'J', 0, nullptr);
    Frame(); Frame();
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{'J'}, "keyboard navigation selects a row for rebinding");
    Capture("07-keyboard-rebound.png");
    Click(Position("Add another input"));
    Click(Position("Move forward"));
    hulk::ControlsMessage(WM_KEYDOWN, 'K', 0, nullptr);
    Frame(); Frame();
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>({'J', 'K'}), "Add another input preserves the existing binding");
    Capture("08-multiple-inputs.png");
    Click(Position("Unbind selected"));
    Check(hulk::bindings::Get()->inputs[0].empty(), "Unbind selected clears that action");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    hulk::ControlsMessage(WM_KEYDOWN, VK_ESCAPE, 0, nullptr);
    Frame();
    Check(!hulk::ControlsOpen() && saves == 2, "Escape closes and saves when capture is inactive");
    ImGui::DestroyContext();
    if (!failures) std::puts("Headless Controls rendering, rebinding, cancellation, conflicts, reset and save passed.");
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try { return Run(argc, argv); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Capture failed: %s\n", error.what());
        return 2;
    }
}
