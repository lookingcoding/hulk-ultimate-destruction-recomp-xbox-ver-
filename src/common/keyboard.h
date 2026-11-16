#pragma once
#include <cstdint>
#include "common/mouse_look.h"
namespace input { struct Pad; }
namespace hulk {
void SetGameWindow(void* hwnd);
void SetMenuModeForTest(int mode);  // -1 = force gameplay, 0 = auto (guest memory), 1 = force menu
bool IsCursorHiddenForTest();
bool KeyboardMessage(void* hwnd, uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result);
void KeyboardPad(int port, input::Pad& pad);
bool InGameplay();  // play, not a menu: the mouse steers the camera and the wheel zooms it
void SetGamepadDetected(bool detected);
bool IsGamepadDetected();

// Mouse and camera controls configuration
// Legacy scalar writes both axes; scalar reads X. UI should use the axis APIs.
void SetMouseSensitivity(float s);
void SetMouseSensitivityX(float s);
float GetMouseSensitivityX();
void SetMouseSensitivityY(float s);
float GetMouseSensitivityY();
float GetMouseSensitivity();
void SetMouseInvertY(bool invert);
bool GetMouseInvertY();
void SetMouseSmoothing(bool smoothing);
bool GetMouseSmoothing();
}
