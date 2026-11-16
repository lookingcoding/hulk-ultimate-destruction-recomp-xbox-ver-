#pragma once
namespace input { struct Pad; }
namespace hulk {
bool BackgroundTest(); // explicit hidden, silent, internal-input fixture
void InitReplay();
void ReplayPad(int port, input::Pad& pad);
}
