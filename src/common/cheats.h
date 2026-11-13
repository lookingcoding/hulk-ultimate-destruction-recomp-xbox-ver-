#pragma once
#include <cstddef>
#include <cstdint>

struct X86Context;

// Cheats (issue #12): the Options -> Cheats screen (display_menu.cpp), saved in settings.toml (settings_ui.cpp) and
// applied to the game each world update. Every cheat is a selector whose level 0 is off: at level 0 a cheat must
// leave the game exactly as it would be without it. Cheats never write unlocks or progression to the game's save.
namespace hulk::cheats {

enum class Cheat : uint8_t {
    // combat and stats: game/hooks.cpp, game/cheat_stats.cpp
    Invulnerability,  // god mode: the disc's own invulnerability
    InfiniteRage,     // the rage (adrenaline) meter stays full, so critical moves never run out
    InfiniteHealth,   // health refills
    Damage,           // the player's damage: normal, x2, x4, one-hit kills
    NoHeat,           // no wanted/heat level from the army
    GameSpeed,        // the world's clock: normal, 75%, 50%, 25%
    // movement and physics: game/cheat_movement.cpp (the issue-12-cheats-movement branch)
    SuperJump,
    LowGravity,
    SuperSpeed,
    Count
};
inline constexpr size_t kCheatCount = size_t(Cheat::Count);

// What the settings and the menu know about a cheat. key is its settings.toml key, levels the number of choices
// including off (2 for an off/on toggle); a toggle is saved as a bool, a cheat with more levels as an integer.
struct CheatInfo {
    Cheat cheat;
    const char* key;
    uint8_t levels;
};
const CheatInfo& Info(Cheat cheat);
const CheatInfo* FindInfo(const char* key);

// A snapshot of every cheat's level. The menu writes levels from the frontend's thread; the world update reads a
// snapshot (Current) on the guest's update thread.
struct CheatState {
    uint8_t level[kCheatCount] = {};
    uint8_t operator[](Cheat c) const { return level[size_t(c)]; }
    bool On(Cheat c) const { return level[size_t(c)] != 0; }
    bool Any() const {
        for (uint8_t l : level)
            if (l) return true;
        return false;
    }
};
CheatState Current();
int Level(Cheat cheat);
// The player changed a cheat in the menu: stored, and settings.toml is written (NoteSettingChanged). Out-of-range
// levels are clamped. Returns whether it changed.
bool Select(Cheat cheat, int level);
// settings.toml: a missing or invalid key is off. Does not mark the settings changed.
void Load(Cheat cheat, int level);

// Applying cheats to the game. Each applier is called on the guest's update thread once per world update step, after
// the world's own update (game/simulation.cpp), with the current snapshot, also when every cheat is off, so an
// applier sees a cheat switch off and puts the game back. Appliers find the player again on every call: the world
// and its actors are destroyed and remade between missions, on restart and on returning to the frontend. They are
// registered from static initializers (a function-local registry, so order does not matter) and never unregistered.
using Applier = void (*)(const CheatState& state, X86Context& ctx, uint8_t* base);
bool RegisterApplier(const char* name, Applier applier);
void RunAppliers(X86Context& ctx, uint8_t* base);  // game/simulation.cpp, after each world update step

// The Cheats screen is open: the native invulnerability selector's callback records the player's choice only then.
void SetScreenActive(bool active);
bool ScreenActive();

// The movement branch's entry point (game/cheat_movement.cpp), registered there with RegisterApplier.
void ApplyMovement(const CheatState& state, X86Context& ctx, uint8_t* base);
}  // namespace hulk::cheats
