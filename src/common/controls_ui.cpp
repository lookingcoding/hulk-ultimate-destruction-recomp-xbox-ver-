#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "common/controls_ui.h"
#include "common/bindings.h"
#include "common/display_menu.h"
#include "app/app.h"
#include "core/input.h"
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <optional>

namespace hulk {
namespace {
std::atomic<bool> opened{false};
std::mutex state_mutex;
ControlsMouseHooks mouse{};
std::optional<bindings::Action> capturing;
std::optional<bindings::Input> captured;
std::atomic<uint16_t> controller{0};
std::atomic<bool> controller_armed{false};
ImGuiConfigFlags previous_config = 0;
ImGuiBackendFlags previous_backend = 0;
std::atomic<unsigned> focus_generation{0};

void Changed() { NoteSettingChanged(); }
void Close() {
    capturing.reset();
    captured.reset();
    opened.store(false);
    controller.store(0);
    auto& io = ImGui::GetIO();
    io.ConfigFlags = previous_config;
    io.BackendFlags = previous_backend;
    for (auto key : {ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadLeft,
        ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadFaceDown, ImGuiKey_GamepadFaceRight})
        io.AddKeyEvent(key, false);
    app::Save();
}
}
void SetControlsMouseHooks(ControlsMouseHooks hooks) {
    std::lock_guard lock(state_mutex);
    mouse = hooks;
}
void OpenControls() {
    std::lock_guard lock(state_mutex);
    if (opened.load()) return;
    capturing.reset();
    captured.reset();
    controller.store(0);
    controller_armed.store(false);
    opened.store(true);
    focus_generation.fetch_add(1);
}
bool ControlsOpen() { return opened.load(); }

bool ControlsMessage(uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if (!ControlsOpen()) return false;
    std::lock_guard lock(state_mutex);
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wp)) {
        capturing.reset();
        captured.reset();
        controller.store(0);
    }
    std::optional<bindings::Input> input;
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lp & (intptr_t(1) << 30))) {
        auto code = bindings::KeyInput(uint32_t(wp), lp);
        if (!bindings::InputName(code).empty()) input = code;
    }
    if (message == WM_LBUTTONDOWN) input = VK_LBUTTON;
    if (message == WM_RBUTTONDOWN) input = VK_RBUTTON;
    if (message == WM_MBUTTONDOWN) input = VK_MBUTTON;
    if (message == WM_XBUTTONDOWN) input = HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    if (message == WM_MOUSEWHEEL && int16_t(HIWORD(wp))) input = int16_t(HIWORD(wp)) > 0 ? bindings::WheelUp : bindings::WheelDown;
    if (message == WM_MOUSEHWHEEL && int16_t(HIWORD(wp))) input = int16_t(HIWORD(wp)) > 0 ? bindings::WheelRight : bindings::WheelLeft;
    if (capturing && input && !captured) captured = input;
    const bool consume = (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
        (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || message == WM_INPUT;
    if (consume && result) *result = message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;
    return consume;
}

bool ControlsPad(int port, input::Pad& pad) {
    if (!ControlsOpen()) return false;
    if (port == 0) {
        uint16_t nav = pad.buttons & 0x3f;
        if (pad.analog[0]) nav |= 0x100;
        if (pad.analog[1]) nav |= 0x200;
        if (pad.ly > 16000) nav |= 1;
        if (pad.ly < -16000) nav |= 2;
        if (pad.lx < -16000) nav |= 4;
        if (pad.lx > 16000) nav |= 8;
        if (!nav) controller_armed.store(true);
        controller.store(controller_armed.load() ? nav : 0);
    }
    const bool connected = pad.connected;
    pad = {};
    pad.connected = connected;
    return true;
}

void DrawControls(double) {
    if (TakeControlsRequest()) OpenControls();
    if (!ControlsOpen()) return;
    std::lock_guard lock(state_mutex);
    auto& io = ImGui::GetIO();

    const uint16_t nav = controller.load();
    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (nav & 1) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (nav & 2) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (nav & 4) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (nav & 8) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (nav & 0x100) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (nav & 0x200) != 0);

    static unsigned last_generation = 0;
    static int selected = 0;
    static bool append = false;
    static std::optional<bindings::Action> pending_action;
    static std::optional<bindings::Input> pending_input;
    static bool focus_cancel = false;
    const unsigned generation = focus_generation.load();
    const bool first_frame = generation != last_generation;
    if (first_frame) {
        last_generation = generation;
        previous_config = io.ConfigFlags;
        previous_backend = io.BackendFlags;
        pending_action.reset(); pending_input.reset();
        ImGui::SetNextWindowFocus();
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    const ImVec2 available = io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(available.x * .5f, available.y * .5f), ImGuiCond_Always, ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(available.x > 760 ? 720 : available.x * .95f,
        available.y > 840 ? 800 : available.y * .95f), ImGuiCond_Always);
    bool visible = true;
    const bool contents = ImGui::Begin("Controls / Keybinds", &visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    if (!visible) {
        ImGui::End();
        Close();
        return;
    }
    if (contents) {
        ImGui::TextWrapped("Select an action, then press a key or mouse button. Tab/arrows and Enter navigate; controller A selects, B goes back.");
        ImGui::TextWrapped("Escape can be bound. Use Cancel capture to cancel. Shared gameplay/menu inputs are allowed.");
        const bool block_edit = capturing.has_value() || pending_action.has_value();
        const bool canceled_capture = capturing.has_value() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
        if (canceled_capture) { capturing.reset(); captured.reset(); }
        ImGui::BeginDisabled(block_edit);
        ImGui::Checkbox("Add another input (keep existing bindings)", &append);
        ImGui::EndDisabled();
        if (capturing) {
            ImGui::Text("Listening: %s", bindings::Info(*capturing).label);
            if (ImGui::Button("Cancel capture") || ImGui::IsItemClicked()) { capturing.reset(); captured.reset(); }
        }
        if (capturing && captured) {
            pending_action = capturing; pending_input = captured;
            capturing.reset(); captured.reset();
            const auto snapshot = bindings::Get();
            if (bindings::Conflicts(*snapshot, *pending_action, *pending_input).empty()) {
                auto map = *snapshot;
                bindings::Assign(map, *pending_action, *pending_input, append, false);
                bindings::Set(std::move(map)); Changed();
                pending_action.reset(); pending_input.reset();
            } else { ImGui::OpenPopup("Binding conflict"); focus_cancel = true; }
        }
        auto snapshot = bindings::Get();
        ImGui::BeginDisabled(block_edit);
        if (ImGui::BeginChild("Actions", ImVec2(0, ImGui::GetContentRegionAvail().y * .55f), true)) {
            for (size_t i = 0; i < bindings::ActionCount; ++i) {
                const auto action = bindings::Action(i);
                const std::string row = std::string(bindings::Info(action).label) + "    [" + bindings::Label(*snapshot, action) + "]";
                ImGui::PushID(int(i));
                if (ImGui::Selectable(row.c_str(), selected == int(i))) {
                    selected = int(i); capturing = action; captured.reset();
                }
                if (first_frame && i == 0) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("Unbind selected")) {
            auto map = *snapshot; map.inputs[size_t(selected)].clear();
            bindings::Set(std::move(map)); Changed(); capturing.reset(); captured.reset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset to defaults")) ImGui::OpenPopup("Reset controls?");
        ImGui::Separator();
        if (mouse.get_x && mouse.set_x && mouse.get_y && mouse.set_y && mouse.get_invert_y && mouse.set_invert_y) {
            float x = mouse.get_x(), y = mouse.get_y();
            bool invert = mouse.get_invert_y();
            if (ImGui::SliderFloat("Mouse sensitivity X", &x, .1f, 5.f, "%.2fx", ImGuiSliderFlags_Logarithmic)) { mouse.set_x(x); Changed(); }
            if (ImGui::SliderFloat("Mouse sensitivity Y", &y, .1f, 5.f, "%.2fx", ImGuiSliderFlags_Logarithmic)) { mouse.set_y(y); Changed(); }
            if (ImGui::Checkbox("Invert mouse Y", &invert)) { mouse.set_invert_y(invert); Changed(); }
        } else ImGui::TextUnformatted("Mouse axis settings are unavailable.");
        if (ImGui::Button("Save and close")) visible = false;
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Binding conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            if (pending_action && pending_input) {
                ImGui::Text("%s is already used by:", bindings::InputName(*pending_input).c_str());
                for (auto action : bindings::Conflicts(*bindings::Get(), *pending_action, *pending_input))
                    ImGui::BulletText("%s", bindings::Info(action).label);
                auto assign = [&](bool remove) {
                    auto map = *bindings::Get();
                    bindings::Assign(map, *pending_action, *pending_input, append, remove);
                    bindings::Set(std::move(map)); Changed();
                    pending_action.reset(); pending_input.reset(); ImGui::CloseCurrentPopup();
                };
                if (ImGui::Button("Replace conflicting bindings")) assign(true);
                ImGui::SameLine();
                if (ImGui::Button("Keep both")) assign(false);
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { pending_action.reset(); pending_input.reset(); ImGui::CloseCurrentPopup(); }
                if (focus_cancel) { ImGui::SetItemDefaultFocus(); focus_cancel = false; }
            } else ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Reset controls?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted("Restore every keybind, both sensitivities, and mouse inversion?");
            if (ImGui::Button("Reset")) {
                bindings::Reset();
                if (mouse.set_x) mouse.set_x(1.f);
                if (mouse.set_y) mouse.set_y(1.f);
                if (mouse.set_invert_y) mouse.set_invert_y(false);
                Changed(); capturing.reset(); captured.reset(); ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // Escape closes only when it is not being captured or resolving a conflict.
        if (!block_edit && !capturing && !pending_action && !ImGui::IsPopupOpen("Reset controls?") &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) visible = false;
    }
    ImGui::End();
    if (!visible) Close();
}
}  // namespace hulk
