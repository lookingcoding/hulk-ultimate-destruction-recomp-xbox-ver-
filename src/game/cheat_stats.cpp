// Combat and stat cheats (issue #12, cheats.h). Where the disc already has the cheat, its own code runs it: the game
// asks its unlockable manager whether a cheat is on (IsUnlockableOn) at the place the cheat acts, and the hook answers
// yes there while the player has the cheat on. Only those call sites are answered, by their return addresses, so the
// profile's own cheat bits are never changed and the save serializer, which reads the same flags, sees the disc's
// values. The rest act on the player's character each world update or on the damage the player deals.
#include <atomic>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "common/addresses.h"
#include "common/cheats.h"
#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

using hulk::cheats::Cheat;
using hulk::cheats::CheatState;
namespace addr = hulk::addr;

namespace {
// IsUnlockableOn (thiscall manager, id; ret 4) as each gameplay check calls it: the return address of the call, the
// unlockable it asks about, and the cheat that answers it.
struct Site {
    uint32_t ret, id;
    Cheat cheat;
};
constexpr uint32_t kInvulnerability = 0x2E4, kDoubleDamage = 0x14, kRegenerator = 0x2E2, kLowGravity = 0x13;
constexpr Site kSites[] = {
    {addr::kRetInvulnerability, kInvulnerability, Cheat::Invulnerability},  // damage to the player is zero
    {addr::kRetDoubleDamage, kDoubleDamage, Cheat::Damage},                  // the player's damage to characters
    {addr::kRetDoubleDamageProps, kDoubleDamage, Cheat::Damage},             // and to smart props
    {addr::kRetRegenerator, kRegenerator, Cheat::Regeneration},             // the regeneration rate
    {addr::kRetLowGravityCharacter, kLowGravity, Cheat::LowGravity},        // a character's gravity
    {addr::kRetLowGravityWorld, kLowGravity, Cheat::LowGravity},            // the physics world's gravity
    {addr::kRetLowGravityWorldOff, kLowGravity, Cheat::LowGravity},
};

std::atomic<uint8_t> g_levels[hulk::cheats::kCheatCount] = {};  // the update thread's last snapshot, for the hooks
uint8_t LevelNow(Cheat c) { return g_levels[size_t(c)].load(std::memory_order_relaxed); }

// The player's character: the app's actor manager (+0x20) and its main actor (+0x124), 0 outside the game
uint32_t Player() {
    const uint32_t app = kernel::R32(addr::kApp);
    const uint32_t actors = app ? kernel::R32(app + 0x20) : 0;
    return actors ? kernel::R32(actors + 0x124) : 0;
}

constexpr uint32_t kHealth = 0x1B0C, kMaxHealth = 0x1B10, kRageThreshold = 0x1B14, kRageMode = 0x1B28;
float F32(uint32_t a) { return std::bit_cast<float>(kernel::R32(a)); }

bool Logging() {
    static const bool on = std::getenv("HULK_TEST_CHEAT_LOG") != nullptr;
    return on;
}

// Test only (HULK_TEST_CHEAT_PROBE): the words of the player's character that changed, every two seconds
void Probe(uint32_t player) {
    static const bool on = std::getenv("HULK_TEST_CHEAT_PROBE") != nullptr;
    if (!on) return;
    constexpr uint32_t kFrom = 0x1A00, kTo = 0x2400;
    static uint32_t last_player = 0, ticks = 0;
    static uint32_t seen[(kTo - kFrom) / 4];
    if (++ticks % 120) return;
    char line[1024];
    size_t used = 0;
    for (uint32_t o = kFrom, i = 0; o < kTo; o += 4, i++) {
        const uint32_t v = kernel::R32(player + o);
        if (player == last_player && v != seen[i] && used < sizeof(line) - 48)
            used += std::snprintf(line + used, sizeof(line) - used, " %X:%08X(%g)", o, v, std::bit_cast<float>(v));
        seen[i] = v;
    }
    if (player != last_player) LOGI("cheat probe: player %08X", player);
    else if (used) LOGI("cheat probe:%s", line);
    last_player = player;
}

// The Hulk's health is his power (the game's own text): above the threshold at +0x1B14 he is in Critical Mass (+0x1B28
// is 1), the rage state that feeds his damage and his critical moves, which drain it back toward the threshold, up to
// his maximum at +0x1B10 (with the Critical Mass upgrades). Infinite rage keeps it at the maximum, so he stays in
// Critical Mass; infinite health keeps it at least at the threshold. Both go through the game's own setter (thiscall
// value, force) and leave a dead character dead; off, the game's own power is left as it is.
void ApplyPower(const CheatState& state, X86Context& ctx, uint32_t player) {
    const float health = F32(player + kHealth), max = F32(player + kMaxHealth), threshold = F32(player + kRageThreshold);
    float target = 0;
    if (state.On(Cheat::InfiniteRage)) target = max;
    else if (state.On(Cheat::InfiniteHealth)) target = std::min(threshold, max);
    if (!(health > 0) || !(target > 0) || health >= target) return;
    if (Logging()) {
        static unsigned logged = 0;
        if (logged++ < 5) LOGI("cheats: setting power of %08X to %.1f, esp %08X", player, target, ctx.esp);
    }
    X86Context call = ctx;
    call.ecx = player;
    kernel::CallGuest(call, addr::kSetHealth, {std::bit_cast<uint32_t>(target), 1});
    if (Logging()) {
        static unsigned logged = 0;
        if (logged++ < 40)
            LOGI("cheats: %s power %.1f -> %.1f (threshold %.1f, max %.1f, critical mass state %u)",
                 state.On(Cheat::InfiniteRage) ? "infinite rage" : "infinite health", health, F32(player + kHealth),
                 threshold, max, unsigned(kernel::R32(player + kRageMode)));
    }
}

// Game speed: the world clock's scale, through the game's own setter (critical moves slow it the same way). The
// game's own scale is kept as the base: a scale the cheat did not write is the game's, and is scaled again.
void ApplyGameSpeed(const CheatState& state, X86Context& ctx) {
    static uint32_t written = 0, base = 0;
    static bool active = false;
    const uint32_t app = kernel::R32(addr::kApp);
    const uint32_t timer = app ? kernel::R32(app + 0x58) : 0;
    if (!timer) { active = false; return; }
    constexpr float kFactor[] = {1.0f, 0.75f, 0.5f, 0.25f};
    const uint8_t level = state[Cheat::GameSpeed];
    const uint32_t raw = kernel::R32(timer + 0x38);
    if (!active && !level) return;
    if (!active || raw != written) base = raw;  // the game's own scale
    const float scale = float(base) / 65536.0f * kFactor[level];
    X86Context call = ctx;
    call.ecx = timer;
    if (!level) {
        // off: the game's own scale, once
        kernel::CallGuest(call, addr::kTimerSetScale, {std::bit_cast<uint32_t>(float(base) / 65536.0f)});
        active = false;
        LOGI("cheats: game speed back to the game's %.3f", base / 65536.0);
        return;
    }
    if (active && raw == written) return;
    kernel::CallGuest(call, addr::kTimerSetScale, {std::bit_cast<uint32_t>(scale)});
    written = kernel::R32(timer + 0x38);
    active = true;
    LOGI("cheats: game speed %.3f x %.2f = %.3f", base / 65536.0, kFactor[level], written / 65536.0);
}

// Test only (HULK_TEST_CHEAT_LOG): the log is flushed twice a second, and world updates that stop for three seconds
// (menus, loading, or a hang) log every guest thread's stack once per stop
std::atomic<uint64_t> g_heartbeat{0};
void Watchdog() {
    static std::once_flag started;
    std::call_once(started, [] {
        std::thread([] {
            uint64_t last = 0;
            int still = 0;
            bool dumped = false;
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                logx::Flush();  // a test run is stopped from outside: keep the log whole
                const uint64_t now = g_heartbeat.load();
                still = now == last ? still + 1 : 0;
                if (!still) dumped = false;
                last = now;
                if (still == 6 && !dumped) {
                    dumped = true;
                    LOGE("cheats watchdog: no world update for 3 s, guest threads:");
                    for (auto& t : kernel::Threads())
                        if (t->ctx) {
                            LOGE("thread %X", t->id);
                            kernel::LogGuestStack(*t->ctx, 48);
                        }
                    logx::Flush();
                }
            }
        }).detach();
    });
}

void LogRageState(uint32_t player) {
    static uint32_t last_player = 0, last_state = ~0u;
    const uint32_t state = kernel::R32(player + kRageMode);
    if (player == last_player && state == last_state) return;
    LOGI("cheats: critical mass state %u, power %.1f of %.1f (threshold %.1f)", state, F32(player + kHealth),
         F32(player + kMaxHealth), F32(player + kRageThreshold));
    last_player = player, last_state = state;
}

void ApplyStats(const CheatState& state, X86Context& ctx, uint8_t*) {
    if (Logging()) {
        Watchdog();
        g_heartbeat.fetch_add(1);
    }
    for (size_t i = 0; i < hulk::cheats::kCheatCount; i++) g_levels[i].store(state.level[i], std::memory_order_relaxed);
    ApplyGameSpeed(state, ctx);
    const uint32_t player = Player();
    if (!player) return;
    Probe(player);
    ApplyPower(state, ctx, player);
    if (Logging()) LogRageState(player);
}
const bool g_registered = hulk::cheats::RegisterApplier("stats", &ApplyStats);
}  // namespace

// IsUnlockableOn (thiscall manager, id; ret 4): a cheat the player has on answers yes at its own gameplay checks
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_IS_UNLOCKABLE_ON));
X86_FUNC(HULK_SUB_IS_UNLOCKABLE_ON) {
    const uint32_t ret = X86_MEM32(ctx.esp), id = X86_MEM32(ctx.esp + 4);
    for (const Site& s : kSites) {
        if (s.ret != ret || s.id != id || !LevelNow(s.cheat)) continue;
        if (Logging()) {
            static unsigned logged = 0;
            if (logged++ < 40) LOGI("cheats: unlockable %X answered on at %08X", id, ret);
        }
        ctx.eax = (ctx.eax & ~0xFFu) | 1;
        ctx.esp += 8;
        return;
    }
    HULK_IMP(HULK_SUB_IS_UNLOCKABLE_ON)(ctx, base);
}

// A character takes damage (thiscall victim: amount, hit, ...; ret 0x18). The hit names its attacker at +8 when its
// type (+0xC) is a character. Damage x4 doubles the amount on top of the disc's double damage; one-hit kills give the
// player's hits more than any character's health. Only the call's own argument changes.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_APPLY_DAMAGE));
X86_FUNC(HULK_SUB_APPLY_DAMAGE) {
    const uint32_t victim = ctx.ecx, hit = X86_MEM32(ctx.esp + 8);
    const uint8_t level = LevelNow(Cheat::Damage);
    const uint32_t player = (level || Logging()) ? Player() : 0;
    const bool by_player = player && hit && victim != player && X86_MEM32(hit + 0xC) == 1 &&
                           X86_MEM32(hit + 8) == player;
    const float amount = std::bit_cast<float>(uint32_t(X86_MEM32(ctx.esp + 4)));
    float scaled = amount;
    if (by_player && level == 2) scaled = amount * 2;
    if (by_player && level == 3 && amount > 0) scaled = 1.0e6f;
    if (scaled != amount) X86_MEM32(ctx.esp + 4) = std::bit_cast<uint32_t>(scaled);
    const float before = victim ? F32(victim + kHealth) : 0;
    HULK_IMP(HULK_SUB_APPLY_DAMAGE)(ctx, base);
    if (Logging() && (by_player || (player && victim == player))) {
        static unsigned logged = 0;
        if (logged++ < 200)
            LOGI("cheats: damage %s %08X amount %.2f -> %.2f health %.1f -> %.1f (damage level %u, invulnerability %u)",
                 by_player ? "by player to" : "to player", victim, amount, scaled, before, F32(victim + kHealth),
                 unsigned(level), unsigned(LevelNow(Cheat::Invulnerability)));
    }
}
