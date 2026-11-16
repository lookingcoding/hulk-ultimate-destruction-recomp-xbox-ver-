#pragma once
#include <cstdint>
namespace input { struct Pad; }
namespace hulk {
// Install once before boot with the independent mouse axis getters/setters.
// Values are multipliers, 0.1..5.0. Settings persistence remains in settings_ui.
struct ControlsMouseHooks {
    float (*get_x)(); void (*set_x)(float);
    float (*get_y)(); void (*set_y)(float);
    bool (*get_invert_y)(); void (*set_invert_y)(bool);
};
void SetControlsMouseHooks(ControlsMouseHooks hooks);
void OpenControls(); // native Options callback; any thread
bool ControlsOpen(); // keyboard: release cursor, reset held keys/latches, suppress guest input while true
void DrawControls(double fps); // GameInfo::ui, renderer thread, runtime UiLock held
bool ControlsMessage(uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result);
// Original physical pad, BEFORE keyboard merging. Suppresses this report while
// open and feeds controller navigation to ImGui. Returns true when suppressed.
bool ControlsPad(int port, input::Pad& pad);
}  // namespace hulk
