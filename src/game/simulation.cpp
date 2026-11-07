#include <bit>
#include <chrono>
#include <cstdlib>
#include "common/simulation.h"
#include "common/simulation_context.h"
#include "common/addresses.h"
#include "cpu/x86_ops.h"
#include "core/log.h"

// CWorld's ordered update: agents/actors -> smart props -> physics -> actor post-collision ->
// mission/camera/scene updates. The renderer (1B9CC0/1BD100 EU) runs outside this function.
// Do not clamp dt on every rendered frame: that would advance simulation too fast at 120/144.
#ifdef HULK_USA
#define WORLD_UPDATE sub_00088C30
#define WORLD_UPDATE_ORIGINAL __imp__sub_00088C30
#define WORLD_ELAPSED sub_000860C0
#define WORLD_ELAPSED_ORIGINAL __imp__sub_000860C0
#define WORLD_UNSCALED sub_00085F80
#define WORLD_UNSCALED_ORIGINAL __imp__sub_00085F80
#else
#define WORLD_UPDATE sub_00088CD0
#define WORLD_UPDATE_ORIGINAL __imp__sub_00088CD0
#define WORLD_ELAPSED sub_000860A0
#define WORLD_ELAPSED_ORIGINAL __imp__sub_000860A0
#define WORLD_UNSCALED sub_00085F60
#define WORLD_UNSCALED_ORIGINAL __imp__sub_00085F60
#endif
namespace {
bool AuditEnabled() {
    static const bool enabled = std::getenv("HULK_TEST_SIMULATION_AUDIT") != nullptr;
    return enabled;
}
void AuditSchedule(float elapsed, const hulk::SimulationSteps& steps) {
    if (!AuditEnabled()) return;
    static unsigned entries = 0, zero = 0, positive_ticks = 0, zero_ticks = 0, flips = 0;
    static int mode = -1;
    const int next_mode = elapsed == 0 ? 0 : 1;
    if (mode != -1 && mode != next_mode) ++flips;
    mode = next_mode; ++entries;
    if (!next_mode) { ++zero; zero_ticks += steps.count; } else positive_ticks += steps.count;
    static auto next = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    const auto now = std::chrono::steady_clock::now();
    if (now < next) return;
    LOGI("simulation audit: entries=%u zero_entries=%u scheduled_pos=%u scheduled_zero=%u mode_changes=%u",
         entries, zero, positive_ticks, zero_ticks, flips);
    entries = zero = positive_ticks = zero_ticks = flips = 0;
    next = now + std::chrono::seconds(1);
}
}
#ifdef HULK_SIMULATION_TEST_CLOCK
namespace hulk { float TestSimulationFrameElapsed(); }
#endif
X86_EXTERN_FUNC(WORLD_ELAPSED_ORIGINAL);
X86_FUNC(WORLD_ELAPSED) {
    const auto* tick = hulk::CurrentSimulationTick();
    if (tick && AuditEnabled()) LOG_FIRST(12, "clock audit: receiver=%08X world_clock=%08X fixed=%u delta=%.6f caller=%08X",
        ctx.ecx, tick->clock, unsigned(tick->clock && ctx.ecx == tick->clock), tick->elapsed, uint32_t(X86_MEM32(ctx.esp)));
    if (!tick || !tick->clock || ctx.ecx != tick->clock) { WORLD_ELAPSED_ORIGINAL(ctx, base); return; }
    x87_push(ctx, tick->elapsed);
    ctx.esp += 4;  // thiscall, no arguments; floating result in ST(0)
}
X86_EXTERN_FUNC(WORLD_UPDATE_ORIGINAL);
X86_EXTERN_FUNC(WORLD_UNSCALED_ORIGINAL);
X86_FUNC(WORLD_UNSCALED) {
    const auto* tick = hulk::CurrentSimulationTick();
    // Paused world helpers (including 1C0EF0 EU) intentionally use unscaled time when the
    // supplied simulation delta is zero. Each housekeeping call now represents 1/60 second
    // of wall time: using the render delta here made cinematics take twice as long at 120 fps.
    // Keep other clocks and positive/scaled simulation behavior on their original path.
    if (!tick || tick->elapsed != 0 || !tick->clock || ctx.ecx != tick->clock) {
        WORLD_UNSCALED_ORIGINAL(ctx, base);
        return;
    }
    if (AuditEnabled()) LOG_FIRST(8, "paused raw clock audit: clock=%08X delta=%.6f caller=%08X",
                                 ctx.ecx, hulk::SimulationClock::kStep, uint32_t(X86_MEM32(ctx.esp)));
    x87_push(ctx, hulk::SimulationClock::kStep);
    ctx.esp += 4;
}
X86_FUNC(WORLD_UPDATE) {
    static hulk::WorldSimulationClock clock;
    static uint32_t owner = 0;
#ifdef HULK_SIMULATION_TEST_CLOCK
    float wall_elapsed = hulk::TestSimulationFrameElapsed();
#else
    const auto now = std::chrono::steady_clock::now();
    static auto previous = now;
    float wall_elapsed = std::chrono::duration<float>(now - previous).count();
    previous = now;
#endif
    const uint32_t stack = ctx.esp, world = ctx.ecx;
    const uint32_t state = X86_MEM32(ctx.esp + 4), elapsed_bits = X86_MEM32(ctx.esp + 8);
    const float elapsed = std::bit_cast<float>(elapsed_bits);
    // States 6/7 take a separate non-world branch in the original; preserve that behavior.
    if (owner != world) { clock.Reset(); owner = world; wall_elapsed = 0; }
    if (X86_MEM32(state) == 6 || X86_MEM32(state) == 7) {
        clock.Reset();
        WORLD_UPDATE_ORIGINAL(ctx, base);
        return;
    }
    // Zero-time housekeeping is clocked at 60 Hz too, with physics still frozen at dt=0.
    const auto steps = clock.Advance(elapsed, wall_elapsed);
    AuditSchedule(elapsed, steps);
    if (!steps.count) { ctx.esp += 12; return; }  // thiscall, two arguments, ret 8
    const auto entry = ctx;
    struct RestoreArguments {
        uint8_t* base;
        uint32_t at, state, elapsed;
        ~RestoreArguments() {
            *reinterpret_cast<uint32_t*>(base + at) = state;
            *reinterpret_cast<uint32_t*>(base + at + 4) = elapsed;
        }
    } restore{base, stack + 4, state, elapsed_bits};
    for (unsigned i = 0; i < steps.count; ++i) {
        ctx = entry;
        // The original reuses both argument slots for camera temporaries. A catch-up tick must
        // reconstruct its call frame, not inherit the previous tick's overwritten arguments.
        X86_MEM32(stack + 4) = state;
        X86_MEM32(stack + 8) = std::bit_cast<uint32_t>(steps.elapsed);
        const uint32_t app = X86_MEM32(hulk::addr::kApp);
        const uint32_t world_clock = app ? uint32_t(X86_MEM32(app + 0x58)) : 0;
        hulk::SimulationTickScope tick(world_clock, steps.elapsed);
        WORLD_UPDATE_ORIGINAL(ctx, base);
    }
}
