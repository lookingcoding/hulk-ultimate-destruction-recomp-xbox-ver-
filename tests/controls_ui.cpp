#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <imgui.h>
#include <imgui_internal.h>
#include "common/bindings.h"
#include "common/controls_ui.h"
#include "core/input.h"

namespace {
std::unordered_map<ImGuiID, ImRect> rectangles;
std::unordered_map<std::string, ImRect> items;
bool requested = false;
int changes = 0, saves = 0, failures = 0;
float sensitivity_x = 1, sensitivity_y = 1;
bool invert_y = false;
void Check(bool value, const char* what) {
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", what); }
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

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 900); io.DeltaTime = 1.f / 60;
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
    Click(Position("Mouse sensitivity X"));
    Check(sensitivity_x != 1 && sensitivity_y == 1, "X slider changes X independently");
    Click(Position("Invert mouse Y"));
    Check(invert_y, "invert Y checkbox changes the setting");
    changes = 0;
    input::Pad pad{}; pad.connected = true; pad.buttons = 8; pad.analog[0] = 255;
    Check(hulk::ControlsPad(0, pad) && pad.connected && !pad.buttons && !pad.analog[0], "Controls suppresses gamepad reports");
    Click(Position("Move forward"));
    hulk::ControlsMessage(WM_KEYDOWN, 'K', 0, nullptr);
    Frame();
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{'K'} && changes == 1,
        "selecting a row and pressing a key updates its binding");
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
    Click(Position("Replace conflicting bindings"));
    Check(hulk::bindings::Get()->inputs[0] == std::vector<hulk::bindings::Input>{'S'} && hulk::bindings::Get()->inputs[1].empty(),
        "conflict replacement removes the conflicting action binding");
    sensitivity_x = 3; sensitivity_y = 2; invert_y = true;
    Click(Position("Reset to defaults")); Frame();
    Click(Position("Reset"));
    Check(hulk::bindings::Get()->inputs == hulk::bindings::Defaults().inputs && sensitivity_x == 1 && sensitivity_y == 1 && !invert_y,
        "Reset restores bindings and all mouse settings");
    Click(Position("Save and close"));
    Check(!hulk::ControlsOpen() && saves == 1, "Save and close persists settings and closes Controls");
    Check(!hulk::ControlsMessage(WM_KEYDOWN, 'K', 0, nullptr), "closed Controls passes input through");
    ImGui::DestroyContext();
    if (!failures) std::puts("Headless Controls rendering, rebinding, cancellation, conflicts, reset and save passed.");
    return failures ? 1 : 0;
}
