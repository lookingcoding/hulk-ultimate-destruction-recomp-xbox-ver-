#pragma once

namespace hulk {
void DrawSettingsUi(double fps);
void LoadCustomSettings(const void* table);
void SaveCustomSettings(void* table);
}
