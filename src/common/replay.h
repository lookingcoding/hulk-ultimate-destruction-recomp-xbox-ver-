#pragma once
namespace input { struct Pad; }
namespace hulk {
void InitReplay();
// Explicit test mode: exact executable path + replay file required. Never desktop input.
bool InternalPadTestMode();
void ReplayPad(int port, input::Pad& pad);
}
