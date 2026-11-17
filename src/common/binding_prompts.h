#pragma once
#include "common/bindings.h"
#include <string>
#include <string_view>

namespace hulk {
void SetHudHintPrompt(std::string_view key, std::string_view button);
std::u16string BindingPrompt(bindings::Action action, int language, bool alternate = true);
// Templates survive in the game's cached text; bindings are resolved just before native layout.
std::u16string ExpandBindingPrompts(std::u16string_view text);
std::string BindingPromptTemplate(std::string_view text, int language);
}
