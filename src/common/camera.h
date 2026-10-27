#pragma once

// Camera Options' Invert X and Invert Y as a PC setting. The game keeps them in two flags (addresses.h) that it shows
// and plays as inverted when zero. As a game starts they are zeroed by something past every function that names them
// (a copy over the memory around them), so the host keeps the player's choice in settings.toml and puts it back.
namespace hulk {
void SetCameraInvert(bool x, bool y);  // from settings.toml
bool GetCameraInvertX();
bool GetCameraInvertY();
// the player changed one in Camera Options (hooks.cpp): the flags as they are now become the setting
void CameraInvertChanged();
// the flags as the setting has them, once the image is loaded and each polled pad
void KeepCameraInvert();
}  // namespace hulk
