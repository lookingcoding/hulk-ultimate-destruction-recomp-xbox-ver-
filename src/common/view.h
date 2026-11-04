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
// Camera distance (settings.toml's camera_distance, set by the mouse wheel in play): percent of the chase camera's
// distance behind Hulk. The chase camera eases its distance (+0x444) toward a base distance (+0x440,
// cm_SetChaseCamBaseDist) as it updates; hooks.cpp scales that base around each update, so the game's own easing and
// combat pull-back carry on at the new distance. A wheel notch changes the distance by kCameraZoomStep; the scale the
// camera gets glides to it (TickCameraDistance) rather than jumping, so a spin of the wheel is one smooth zoom.
inline constexpr int kMinCameraDistance = 40, kMaxCameraDistance = 250;  // percent
inline constexpr float kCameraZoomStep = 1.12f;                          // a notch: 12% farther, or closer by as much
inline constexpr float kCameraZoomRate = 8.0f;                           // the glide's speed, per second
void SetCameraDistance(int percent);  // clamped to kMinCameraDistance..kMaxCameraDistance; the camera glides to it
int GetCameraDistance();
// notches closer (steps < 0) or farther; false when already at that end
bool StepCameraDistance(int steps);
// each chase camera update: the applied scale glides toward the setting over seconds; the first call starts there
void TickCameraDistance(float seconds);
float AdjustCameraDistance(float distance);  // by the applied scale
}  // namespace hulk
