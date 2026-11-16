#pragma once
#include "common/bindings.h"
namespace input { struct Pad; }
namespace hulk {
void ApplyInputBindings(const bindings::Map& map, const bindings::InputState& state,
                        bool in_menu, bool selector, input::Pad& pad);
}
