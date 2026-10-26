#pragma once
namespace input { struct Pad; }
namespace hulk {
void InitReplay();
void ReplayPad(int port, input::Pad& pad);
}
