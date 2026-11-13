#include "common/cheats.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
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
    {Cheat::GameSpeed, "cheat_game_speed", 4},
    {Cheat::SuperJump, "cheat_super_jump", 2},
    {Cheat::LowGravity, "cheat_low_gravity", 2},
    {Cheat::Regeneration, "cheat_regeneration", 2},
};
static_assert([] {
    for (size_t i = 0; i < kCheatCount; i++)
        if (size_t(kInfo[i].cheat) != i) return false;
    return true;
}());

std::atomic<uint8_t> g_level[kCheatCount] = {};

// Registered from static initializers, before any world update; a fixed table so the update thread never allocates
struct Registry {
    std::mutex lock;
    std::pair<const char*, Applier> appliers[8] = {};
    std::atomic<size_t> count{0};
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
    const size_t n = r.count.load();
    if (n == std::size(r.appliers)) return false;
    r.appliers[n] = {name, applier};
    r.count.store(n + 1);
    return true;
}

namespace {
// Test only: HULK_TEST_CHEATS="20000:cheat_infinite_rage=1;60000:cheat_infinite_rage=0" selects each cheat at that
// many milliseconds after the first world update, through the same path as the menu
struct Scheduled {
    long at;
    Cheat cheat;
    int level;
};
std::vector<Scheduled> ParseSchedule(const char* text) {
    std::vector<Scheduled> out;
    std::string all = text ? text : "";
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find(';', start);
        if (end == std::string::npos) end = all.size();
        const std::string item = all.substr(start, end - start);
        start = end + 1;
        const size_t colon = item.find(':'), equals = item.find('=');
        if (colon == std::string::npos || equals == std::string::npos || equals < colon) continue;
        const CheatInfo* info = FindInfo(item.substr(colon + 1, equals - colon - 1).c_str());
        if (!info) continue;
        out.push_back({std::atol(item.c_str()), info->cheat, std::atoi(item.c_str() + equals + 1)});
    }
    return out;
}
void RunSchedule() {
    static const std::vector<Scheduled> schedule = ParseSchedule(std::getenv("HULK_TEST_CHEATS"));
    if (schedule.empty()) return;
    static const auto started = std::chrono::steady_clock::now();
    static size_t next = 0;
    const long elapsed = long(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    while (next < schedule.size() && schedule[next].at <= elapsed) {
        const Scheduled& s = schedule[next++];
        LOGI("cheats: test schedule at %ld ms selects %s = %d", elapsed, Info(s.cheat).key, s.level);
        Select(s.cheat, s.level);
    }
}
}  // namespace

void RunAppliers(X86Context& ctx, uint8_t* base) {
    RunSchedule();
    Registry& r = Appliers();
    const CheatState state = Current();
    for (size_t i = 0, n = r.count.load(); i < n; i++) r.appliers[i].second(state, ctx, base);
}

}  // namespace hulk::cheats
