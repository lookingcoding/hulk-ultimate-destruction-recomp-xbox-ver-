#include "common/cheats.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>
#include "common/display_menu.h"
#include "core/log.h"

namespace hulk::cheats {
namespace {
constexpr CheatInfo kInfo[kCheatCount] = {
    {Cheat::Invulnerability, "cheat_invulnerability", 2},
    {Cheat::InfiniteRage, "cheat_infinite_rage", 2},
    {Cheat::InfiniteHealth, "cheat_infinite_health", 2},
    {Cheat::Damage, "cheat_damage", 4},
    {Cheat::NoHeat, "cheat_no_heat", 2},
    {Cheat::GameSpeed, "cheat_game_speed", 4},
    {Cheat::SuperJump, "cheat_super_jump", 2},
    {Cheat::LowGravity, "cheat_low_gravity", 2},
    {Cheat::SuperSpeed, "cheat_super_speed", 2},
};
static_assert([] {
    for (size_t i = 0; i < kCheatCount; i++)
        if (size_t(kInfo[i].cheat) != i) return false;
    return true;
}());

std::atomic<uint8_t> g_level[kCheatCount] = {};
std::atomic<bool> g_screen_active{false};

struct Registry {
    std::mutex lock;
    std::vector<std::pair<const char*, Applier>> appliers;
};
Registry& Appliers() {
    static Registry registry;
    return registry;
}

uint8_t Clamp(Cheat cheat, int level) {
    return uint8_t(std::clamp(level, 0, int(Info(cheat).levels) - 1));
}
}  // namespace

const CheatInfo& Info(Cheat cheat) { return kInfo[size_t(cheat)]; }

const CheatInfo* FindInfo(const char* key) {
    for (const CheatInfo& info : kInfo)
        if (key && std::strcmp(info.key, key) == 0) return &info;
    return nullptr;
}

CheatState Current() {
    CheatState state;
    for (size_t i = 0; i < kCheatCount; i++) state.level[i] = g_level[i].load(std::memory_order_relaxed);
    return state;
}

int Level(Cheat cheat) { return g_level[size_t(cheat)].load(); }

bool Select(Cheat cheat, int level) {
    const uint8_t value = Clamp(cheat, level);
    if (g_level[size_t(cheat)].exchange(value) == value) return false;
    LOGR("cheats: %s set to %u", Info(cheat).key, unsigned(value));
    NoteSettingChanged();
    return true;
}

void Load(Cheat cheat, int level) { g_level[size_t(cheat)].store(level < 0 ? 0 : Clamp(cheat, level)); }

bool RegisterApplier(const char* name, Applier applier) {
    if (!applier) return false;
    Registry& r = Appliers();
    std::lock_guard guard(r.lock);
    r.appliers.emplace_back(name, applier);
    return true;
}

void RunAppliers(X86Context& ctx, uint8_t* base) {
    Registry& r = Appliers();
    std::vector<std::pair<const char*, Applier>> appliers;
    {
        std::lock_guard guard(r.lock);
        appliers = r.appliers;
    }
    const CheatState state = Current();
    for (auto& [name, applier] : appliers) applier(state, ctx, base);
}

void SetScreenActive(bool active) { g_screen_active.store(active); }
bool ScreenActive() { return g_screen_active.load(); }
}  // namespace hulk::cheats
