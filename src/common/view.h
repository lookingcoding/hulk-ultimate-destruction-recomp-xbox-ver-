#pragma once
#include <cstdint>

// Field of view and render distance as PC settings (settings.toml's fov_increase and render_distance, and rows of
// Display Options). The game's render manager hands its camera to the scene's render layer whenever the camera
// changes (addresses.h, HULK_SUB_SET_VIEW): the field of view (horizontal, in radians, already widened for a
// widescreen TV), aspect, near and far planes. The layer sets up its Pure3D camera, the frustum and its own culling
// angles from those, so a wider field and a farther plane given there are drawn and culled alike. The mission scripts'
// fog planes (rm_SetFogPlanes) go to each frame (HULK_SUB_APPLY_FOG) and move out with the far plane, or the extra
// distance would draw into solid fog. hooks.cpp applies both.
namespace hulk {
inline constexpr int kMaxFovIncrease = 60;                         // degrees
inline constexpr int kMinRenderDistance = 50, kMaxRenderDistance = 1000;  // percent
inline constexpr float kMaxFov = 170.0f;                           // degrees: the widest field given to the camera

void SetFovIncrease(int degrees);  // clamped to 0..kMaxFovIncrease
int GetFovIncrease();
void SetRenderDistance(int percent);  // clamped to kMinRenderDistance..kMaxRenderDistance
int GetRenderDistance();
// changes each time either setting does: the hooks set the camera up again on the next frame
uint32_t ViewGeneration();

// the game's horizontal field of view (radians) widened by the setting, never past kMaxFov nor below the game's own
float AdjustFov(float radians);
// a far or fog plane distance moved out by the setting
float AdjustDistance(float distance);
}  // namespace hulk
