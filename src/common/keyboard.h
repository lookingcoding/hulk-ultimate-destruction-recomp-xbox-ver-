#pragma once
#include <cstdint>

namespace input { struct Pad; }

namespace hulk {
void SetGameWindow(void* hwnd);
void SetMenuModeForTest(int mode);  // -1 = force gameplay, 0 = auto (guest memory), 1 = force menu
bool IsCursorHiddenForTest();
bool KeyboardMessage(void* hwnd, uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result);
void KeyboardPad(int port, input::Pad& pad);

// Mouse and camera controls configuration
void SetMouseSensitivity(float s);
float GetMouseSensitivity();
void SetMouseInvertY(bool invert);
bool GetMouseInvertY();
void SetMouseSmoothing(bool smoothing);
bool GetMouseSmoothing();

// Native menu state queries
bool IsInMenuState();
bool IsCurrentScreenOptions();
int GetCurrentMenuSelectedRow();
void SetCurrentMenuSelectedRow(int row);
}
