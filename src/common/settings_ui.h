#pragma once
#include <cstdint>

namespace hulk {

void LoadCustomSettings(const void* table_ptr);
void SaveCustomSettings(void* table_ptr);

// Native 1:1 Display Options screen state
bool IsDisplayOptionsOpen();
void SetDisplayOptionsOpen(bool open);

// Draws 1:1 native UI elements (4th row in Options menu, and the Display Options screen)
void DrawNativeDisplayUi(double fps);

// Direct input processing when Display Options screen is open
bool HandleDisplayInputKey(uint32_t vk, bool down);
bool HandleDisplayClick(int cursor_x, int cursor_y, int client_w, int client_h, bool right_click);
void HandleDisplayWheel(int delta);
void HandleDisplayHover(int cursor_x, int cursor_y, int client_w, int client_h);

}  // namespace hulk
