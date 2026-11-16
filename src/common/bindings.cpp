#include "common/bindings.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace hulk::bindings {
namespace {
constexpr ActionInfo actions[] = {
    {"move_up", "Move forward", Both}, {"move_down", "Move backward", Both},
    {"move_left", "Move left", Both}, {"move_right", "Move right", Both},
    {"dpad_up", "D-pad up", Both}, {"dpad_down", "D-pad down", Both},
    {"dpad_left", "D-pad left", Both}, {"dpad_right", "D-pad right", Both},
    {"pause", "Pause / Start", Both}, {"map", "Map / Back button", Both},
    {"reset_camera", "Reset camera / L3", Both}, {"taunt", "Taunt / R3", Both},
    {"jump", "Jump / A (gameplay)", Gameplay}, {"grab", "Grab / B (gameplay)", Gameplay},
    {"attack", "Attack / X (gameplay)", Gameplay}, {"special", "Special / Y", Both},
    {"black", "Black button", Both}, {"white", "White button", Both},
    {"guard", "Guard / Left trigger", Gameplay}, {"sprint", "Sprint / Right trigger", Both},
    {"menu_confirm", "Confirm / A (menus)", Menu}, {"menu_back", "Back / B (menus)", Menu},
    {"menu_delete", "Delete / X (menus)", Menu},
    {"menu_select", "Select / next option (menus)", Menu},
    {"menu_scroll_up", "Scroll menu up", Menu}, {"menu_scroll_down", "Scroll menu down", Menu},
    {"zoom_in", "Camera closer", Gameplay}, {"zoom_out", "Camera farther", Gameplay}
};
static_assert(std::size(actions) == ActionCount);

struct NamedInput { Input code; const char* name; };
constexpr NamedInput names[] = {
    {1,"MouseLeft"},{2,"MouseRight"},{4,"MouseMiddle"},{5,"MouseX1"},{6,"MouseX2"},
    {8,"Backspace"},{9,"Tab"},{13,"Enter"},{16,"Shift"},{17,"Ctrl"},{18,"Alt"},
    {19,"Pause"},{20,"CapsLock"},{27,"Escape"},{32,"Space"},{33,"PageUp"},{34,"PageDown"},
    {35,"End"},{36,"Home"},{37,"Left"},{38,"Up"},{39,"Right"},{40,"Down"},
    {44,"PrintScreen"},{45,"Insert"},{46,"Delete"},{91,"LeftWin"},{92,"RightWin"},
    {106,"NumMultiply"},{107,"NumAdd"},{109,"NumSubtract"},{110,"NumDecimal"},{111,"NumDivide"},
    {144,"NumLock"},{145,"ScrollLock"},{160,"LeftShift"},{161,"RightShift"},
    {162,"LeftCtrl"},{163,"RightCtrl"},{164,"LeftAlt"},{165,"RightAlt"},
    {186,"Semicolon"},{187,"Equals"},{188,"Comma"},{189,"Minus"},{190,"Period"},
    {191,"Slash"},{192,"Backtick"},{219,"LeftBracket"},{220,"Backslash"},
    {221,"RightBracket"},{222,"Quote"},{226,"OEM102"},
    {WheelUp,"WheelUp"},{WheelDown,"WheelDown"},{WheelLeft,"WheelLeft"},{WheelRight,"WheelRight"}
};
std::string Lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
Input GenericModifier(Input code) {
    return code == 160 || code == 161 ? 16 : code == 162 || code == 163 ? 17 : code == 164 || code == 165 ? 18 : 0;
}
bool Overlap(Input a, Input b) {
    return a == b || (GenericModifier(a) && GenericModifier(a) == b) || (GenericModifier(b) && GenericModifier(b) == a);
}
std::atomic<Snapshot> current{std::make_shared<const Map>(Defaults())};
}  // namespace
const ActionInfo& Info(Action a) { return actions[size_t(a)]; }
Map Defaults() {
    Map m;
    m.inputs = {{ {'W'}, {'S'}, {'A'}, {'D'}, {38}, {40}, {37}, {39},
        {27}, {9}, {'C'}, {'V'}, {32,13}, {'Q'}, {1}, {'E'}, {'2'}, {'1'}, {2}, {16},
        {32,1}, {'Q',2}, {46}, {13}, {WheelUp}, {WheelDown}, {WheelUp}, {WheelDown} }};
    return m;
}
Snapshot Get() { return current.load(); }
void Set(Map m) {
    for (auto& list : m.inputs) {
        std::vector<Input> valid;
        for (Input input : list) {
            if (InputName(input).empty()) throw std::invalid_argument("Unknown binding input");
            if (std::find(valid.begin(), valid.end(), input) == valid.end()) valid.push_back(input);
        }
        list = std::move(valid);
    }
    current.store(std::make_shared<const Map>(std::move(m)));
}
void Reset() { Set(Defaults()); }
std::string InputName(Input code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, char(code));
    if (code >= 112 && code <= 135) return "F" + std::to_string(code - 111);
    if (code >= 96 && code <= 105) return "Num" + std::to_string(code - 96);
    for (auto n : names) if (n.code == code) return n.name;
    return {};
}
Input KeyInput(uint32_t key, intptr_t lp) {
    if (key == 16) return ((uintptr_t(lp) >> 16) & 0xff) == 0x36 ? 161 : 160;
    if (key == 17) return (uintptr_t(lp) & (uintptr_t(1) << 24)) ? 163 : 162;
    if (key == 18) return (uintptr_t(lp) & (uintptr_t(1) << 24)) ? 165 : 164;
    return key < 256 ? Input(key) : 0;
}
std::optional<Input> ParseInput(std::string_view name) {
    const std::string lower = Lower(name);
    if (lower == "esc") return 27;
    if (lower == "return") return 13;
    if (lower == "control") return 17;
    if (lower == "lmb") return 1;
    if (lower == "rmb") return 2;
    if (lower == "mmb") return 4;
    for (Input code = 1; code < InputCount; ++code) {
        const auto canonical = InputName(code);
        if (!canonical.empty() && lower == Lower(canonical)) return code;
    }
    return std::nullopt;
}
bool Matches(const Map& m, Action a, Input code) {
    const auto& list = m.inputs[size_t(a)];
    if (std::find(list.begin(), list.end(), code) != list.end()) return true;
    // Win32 reports both side-specific keys and aggregate modifiers. A generic
    // modifier binding accepts either side without making side-specific binds broad.
    const Input generic = GenericModifier(code);
    return generic && std::find(list.begin(), list.end(), generic) != list.end();
}
bool Down(const Map& m, Action a, const InputState& state) {
    for (Input code : m.inputs[size_t(a)]) {
        if (state[code]) return true;
        if (code == 16 && (state[160] || state[161])) return true;
        if (code == 17 && (state[162] || state[163])) return true;
        if (code == 18 && (state[164] || state[165])) return true;
    }
    return false;
}
std::string Label(const Map& m, Action a) {
    std::string label;
    for (Input input : m.inputs[size_t(a)]) {
        if (!label.empty()) label += " / ";
        label += InputName(input);
    }
    return label.empty() ? "Unbound" : label;
}
std::vector<Action> Conflicts(const Map& m, Action a, Input input) {
    std::vector<Action> result;
    for (size_t i = 0; i < ActionCount; ++i) {
        if (i == size_t(a) || !(Info(a).contexts & Info(Action(i)).contexts)) continue;
        for (Input other : m.inputs[i]) {
            if (Overlap(input, other)) {
                result.push_back(Action(i));
                break;
            }
        }
    }
    return result;
}
void Assign(Map& m, Action a, Input input, bool append, bool remove_conflicts) {
    if (InputName(input).empty()) throw std::invalid_argument("Unknown binding input");
    if (remove_conflicts) for (Action other : Conflicts(m, a, input)) {
        auto& list = m.inputs[size_t(other)];
        std::erase_if(list, [&](Input code) { return Overlap(input, code); });
    }
    auto& list = m.inputs[size_t(a)];
    if (!append) list.clear();
    if (std::find(list.begin(), list.end(), input) == list.end()) list.push_back(input);
}
void Load(const void* ptr) {
    Map m = Defaults();
    if (ptr) {
        const auto& root = *static_cast<const toml::table*>(ptr);
        if (auto table = root["bindings"].as_table()) for (size_t i = 0; i < ActionCount; ++i) {
            auto node = (*table)[Info(Action(i)).key];
            if (!node) continue;
            std::vector<Input> list;
            auto parse = [&](const toml::node& n) {
                auto s = n.value_exact<std::string>();
                if (!s) return false;
                if (Lower(*s) == "unbound") return true;
                auto input = ParseInput(*s);
                if (!input) return false;
                list.push_back(*input);
                return true;
            };
            bool valid = true;
            if (auto array = node.as_array()) {
                for (const auto& n : *array) if (!parse(n)) { valid = false; break; }
            } else valid = parse(*node.node());
            if (valid) m.inputs[i] = std::move(list);
        }
    }
    Set(std::move(m));
}
void Save(void* ptr) {
    if (!ptr) return;
    auto snapshot = Get();
    toml::table table;
    for (size_t i = 0; i < ActionCount; ++i) {
        toml::array array;
        for (Input code : snapshot->inputs[i]) array.push_back(InputName(code));
        table.insert(Info(Action(i)).key, std::move(array));
    }
    static_cast<toml::table*>(ptr)->insert_or_assign("bindings", std::move(table));
}
}  // namespace hulk::bindings
