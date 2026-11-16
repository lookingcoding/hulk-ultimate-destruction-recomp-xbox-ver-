#pragma once
#include <cstdint>
#include <string>
#include "common/bindings.h"
struct X86Context;
namespace input { struct Pad; }
namespace hulk {
// All rendering and navigation belong to the game's Dolly frontend.
struct ControlsMouseHooks {
    float (*get_x)(); void (*set_x)(float);
    float (*get_y)(); void (*set_y)(float);
    bool (*get_invert_y)(); void (*set_invert_y)(bool);
};
void SetControlsMouseHooks(ControlsMouseHooks hooks);
std::string NativeControlsScript(const char* title, const char* row, bool main, const std::string& nl);
std::string NativeControlsStrings(); // UTF-8 string-table lines
void RegisterControlsCallbacks(X86Context& ctx);
bool NativeControlsFunction(X86Context& ctx, uint32_t owner);
bool NativeControlsValue(uint32_t owner, uint32_t value);
void TickNativeControls(X86Context& ctx);
bool ControlsCapturing();
// Shared capture boundary for window input and the opt-in internal replay fixture.
bool CaptureControlsInput(bindings::Input input);
void RequestControlsFixture(unsigned command); // ignored unless the hidden fixture is explicitly enabled
bool ControlsMessage(uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result);
void ControlsPad(int port, input::Pad& pad);
} // namespace hulk
