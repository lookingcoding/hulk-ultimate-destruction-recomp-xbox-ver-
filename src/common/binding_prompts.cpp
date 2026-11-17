#include "common/binding_prompts.h"
#include <algorithm>
#include <unordered_map>

namespace hulk {
namespace {
using bindings::Action;
std::unordered_map<std::u16string, Action> hud_actions;
std::u16string Wide(std::string_view s) { return {s.begin(), s.end()}; }
std::u16string InputLabel(bindings::Input input, int language) {
    static constexpr const char16_t* space[] = {u"Space", u"Espace", u"Espacio", u"Spazio"};
    static constexpr const char16_t* enter[] = {u"Enter", u"Entrée", u"Intro", u"Invio"};
    static constexpr const char16_t* shift[] = {u"Shift", u"Maj", u"Mayús", u"Maiusc"};
    static constexpr const char16_t* left[] = {u"Left Click", u"Clic gauche", u"Clic izquierdo", u"Clic sinistro"};
    static constexpr const char16_t* right[] = {u"Right Click", u"Clic droit", u"Clic derecho", u"Clic destro"};
    switch (input) {
        case 32: return space[language];
        case 13: return enter[language];
        case 16: return shift[language];
        case 1: return left[language];
        case 2: return right[language];
        default: return Wide(bindings::InputName(input));
    }
}
std::u16string Keys(const bindings::Map& map, Action action, int language, bool alternate) {
    const auto& keys = map.inputs[size_t(action)];
    if (keys.empty()) {
        static constexpr const char16_t* none[] = {u"Unbound", u"Non attribué", u"Sin asignar", u"Non assegnato"};
        return none[language];
    }
    auto label = InputLabel(keys[0], language);
    if (alternate && keys.size() > 1) label += u" / " + InputLabel(keys[1], language);
    return label;
}
std::u16string Token(const bindings::Map& map, std::u16string_view key, int language) {
    bool compact = key.starts_with(u"short_");
    if (compact) key.remove_prefix(6);
    // Short aliases keep all three footer templates inside the game's fixed text buffer.
    if (key == u"ms") { key = u"menu_select"; compact = true; }
    if (key == u"mb") { key = u"menu_back"; compact = true; }
    if (key == u"md") { key = u"menu_delete"; compact = true; }
    bool hold = key.starts_with(u"hold_");
    if (hold) key.remove_prefix(5);
    std::u16string label;
    if (key.starts_with(u"hint:")) {
        const auto found = hud_actions.find(std::u16string(key.substr(5)));
        return found == hud_actions.end() ? u"" : u"[" + Keys(map, found->second, language, true) + u"]";
    }
    if (key == u"move" || key == u"around") {
        for (auto a : {Action::MoveUp, Action::MoveDown, Action::MoveLeft, Action::MoveRight}) {
            if (key == u"around" && a != Action::MoveLeft && a != Action::MoveRight) continue;
            if (!label.empty()) label += u", ";
            label += Keys(map, a, language, true);
        }
    } else {
        bool found = false;
        for (size_t i = 0; i < bindings::ActionCount; ++i) {
            auto a = Action(i);
            if (key != Wide(bindings::Info(a).key)) continue;
            label = Keys(map, a, language, !compact);
            found = true;
            break;
        }
        if (!found) return {};
    }
    if (hold) {
        static constexpr const char16_t* prefix[] = {u"Hold ", u"Maintenir ", u"Mantener ", u"Tenere "};
        label.insert(0, prefix[language]);
    }
    return u"[" + label + u"]";
}
}
void SetHudHintPrompt(std::string_view key, std::string_view button) {
    if (button.starts_with("\b")) button.remove_prefix(1);
    if (button.starts_with("\\b")) button.remove_prefix(2);
    static constexpr std::pair<std::string_view, Action> codes[] = {
        {"se", Action::Jump}, {"ju", Action::Jump}, {"jc", Action::Jump},
        {"us", Action::Grab}, {"uc", Action::Grab}, {"at", Action::Attack}, {"ac", Action::Attack},
        {"sp", Action::Special}, {"sc", Action::Special}, {"ta", Action::Guard}, {"ru", Action::Sprint}
    };
    auto name = Wide(key);
    for (const auto& [code, action] : codes) if (button == code) { hud_actions[name] = action; return; }
    hud_actions.erase(name);
}
std::u16string BindingPrompt(Action action, int language, bool alternate) {
    auto map = bindings::Get();
    return u"[" + Keys(*map, action, std::clamp(language, 0, 3), alternate) + u"]";
}
std::string BindingPromptTemplate(std::string_view text, int language) {
    std::string out(text);
    size_t at = 0;
    while ((at = out.find("{pc:", at)) != std::string::npos) {
        out.insert(at + 3, 1, char('0' + std::clamp(language, 0, 3)));
        at += 5;
    }
    return out;
}
std::u16string ExpandBindingPrompts(std::u16string_view text) {
    if (text.find(u"{pc") == std::u16string_view::npos) return std::u16string(text);
    const auto map = bindings::Get();
    std::u16string out;
    size_t from = 0;
    while (from < text.size()) {
        const size_t at = text.find(u"{pc", from);
        if (at == std::u16string_view::npos) { out.append(text.substr(from)); break; }
        out.append(text.substr(from, at - from));
        const size_t end = text.find(u'}', at);
        if (end != std::u16string_view::npos && end > at + 5 && text[at + 3] >= u'0' &&
            text[at + 3] <= u'3' && text[at + 4] == u':') {
            auto key = text.substr(at + 5, end - at - 5);
            auto value = Token(*map, key, int(text[at + 3] - u'0'));
            if (!value.empty() || key.starts_with(u"hint:")) { out += value; from = end + 1; continue; }
        }
        out += text[at];
        from = at + 1;
    }
    return out;
}
}
