#include "common/input_mapping.h"
#include "core/input.h"

namespace hulk {
void ApplyInputBindings(const bindings::Map& map, const bindings::InputState& state,
                        bool in_menu, bool selector, input::Pad& pad) {
    using bindings::Action;
    const auto down = [&](Action action) { return bindings::Down(map, action, state); };
    constexpr Action buttons[] = {Action::DpadUp, Action::DpadDown, Action::DpadLeft, Action::DpadRight,
        Action::Pause, Action::Map, Action::ResetCamera, Action::Taunt};
    for (unsigned i = 0; i < std::size(buttons); ++i)
        if (down(buttons[i])) pad.buttons |= uint16_t(1u << i);
    const Action analog[] = {in_menu ? Action::MenuConfirm : Action::Jump,
        in_menu ? Action::MenuBack : Action::Grab, in_menu ? Action::MenuDelete : Action::Attack,
        Action::Special, Action::Black, Action::White, Action::Guard, Action::Sprint};
    for (unsigned i = 0; i < std::size(analog); ++i)
        if ((i != 6 || !in_menu) && down(analog[i])) pad.analog[i] = 255;
    if (in_menu && !selector && down(Action::MenuSelect)) pad.analog[0] = 255;
    const auto axis = [&](Action positive, Action negative) {
        return int16_t((int(down(positive)) - int(down(negative))) * 32767);
    };
    if (const auto x = axis(Action::MoveRight, Action::MoveLeft)) pad.lx = x;
    if (const auto y = axis(Action::MoveUp, Action::MoveDown)) pad.ly = y;
}
}
