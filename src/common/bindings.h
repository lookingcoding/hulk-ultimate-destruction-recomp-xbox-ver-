#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hulk::bindings {
// Keyboard inputs use Win32 virtual keys, mouse buttons use their VK_* codes.
// Wheel inputs are impulses, never held keys. No Windows or renderer dependency.
using Input = uint16_t;
inline constexpr Input WheelUp = 256, WheelDown = 257, WheelLeft = 258, WheelRight = 259;
inline constexpr size_t InputCount = 260;
enum class Action : uint8_t {
    MoveUp, MoveDown, MoveLeft, MoveRight, DpadUp, DpadDown, DpadLeft, DpadRight,
    Pause, Map, ResetCamera, Taunt, Jump, Grab, Attack, Special, Black, White, Guard, Sprint,
    MenuConfirm, MenuBack, MenuDelete, MenuSelect, MenuScrollUp, MenuScrollDown, ZoomIn, ZoomOut, Count
};
inline constexpr size_t ActionCount = size_t(Action::Count);
enum Context : unsigned { Gameplay = 1, Menu = 2, Both = 3 };
struct ActionInfo { const char* key; const char* label; unsigned contexts; };
const ActionInfo& Info(Action action);
struct Map { std::array<std::vector<Input>, ActionCount> inputs; };
using Snapshot = std::shared_ptr<const Map>;
Map Defaults();
Snapshot Get();  // hold once for an entire message/pad poll; immutable across reload/edit
void Set(Map map); // validates input codes, publishes the whole map atomically
void Reset();
std::optional<Input> ParseInput(std::string_view name); // case-insensitive; canonical names from InputName
std::string InputName(Input input);
// WM_KEY* wp/lp -> side-specific modifier; ordinary virtual keys pass through.
Input KeyInput(uint32_t virtual_key, intptr_t lparam);
bool Matches(const Map& map, Action action, Input input);
using InputState = std::array<bool, InputCount>;
bool Down(const Map& map, Action action, const InputState& state);
std::string Label(const Map& map, Action action);
// Only overlapping contexts conflict. Sharing across gameplay/menu is intentional.
std::vector<Action> Conflicts(const Map& map, Action action, Input input);
void Assign(Map& map, Action action, Input input, bool append, bool remove_conflicts);
// [bindings] in the runtime's root toml::table. Missing/invalid entries use that
// action's defaults; [] or "unbound" explicitly disables it. Invalid arrays fall
// back as a whole, never partially. Both functions accept null safely.
void Load(const void* table);
void Save(void* table);
}  // namespace hulk::bindings
