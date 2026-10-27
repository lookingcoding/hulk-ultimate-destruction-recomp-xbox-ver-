#pragma once

namespace hulk {

// Each release of the game has its own executable, recompiled from its default.xbe. When the game in assets is
// another release's and that release's executable is beside this one, starts it with the same arguments and exits;
// otherwise returns, and the runtime goes on to find the game as it does.
void RunMatchingRelease();

}  // namespace hulk
