#include <bit>
#include <cstdlib>
#include "common/simulation.h"
#include "core/log.h"
#include "xdk/hle.h"

// CWorld's ordered update: agents/actors -> smart props -> physics -> actor post-collision ->
// mission/camera/scene updates. The renderer (1B9CC0/1BD100 EU) runs outside this function.
// Do not clamp dt on every rendered frame: that would advance simulation too fast at 120/144.
#ifdef HULK_USA
#define WORLD_UPDATE sub_00088C30
#define WORLD_UPDATE_ORIGINAL __imp__sub_00088C30
#else
#define WORLD_UPDATE sub_00088CD0
#define WORLD_UPDATE_ORIGINAL __imp__sub_00088CD0
#endif
X86_EXTERN_FUNC(WORLD_UPDATE_ORIGINAL);
X86_FUNC(WORLD_UPDATE) {
    static hulk::SimulationClock clock;
    static uint32_t owner = 0;
    const uint32_t stack = ctx.esp, world = ctx.ecx;
    const uint32_t state = X86_MEM32(ctx.esp + 4), elapsed_bits = X86_MEM32(ctx.esp + 8);
    const float elapsed = std::bit_cast<float>(elapsed_bits);
    // Preserve the original paused/frontend branch and discard fractional time at transitions.
    if (owner != world) { clock.Reset(); owner = world; }
    if (X86_MEM32(state) == 6 || X86_MEM32(state) == 7 || elapsed <= 0) {
        clock.Reset();
        WORLD_UPDATE_ORIGINAL(ctx, base);
        return;
    }
    const unsigned steps = clock.Advance(elapsed);
    if (!steps) { ctx.esp += 12; return; }  // thiscall, two arguments, ret 8
    const auto entry = ctx;
    struct RestoreArguments {
        uint8_t* base;
        uint32_t at, state, elapsed;
        ~RestoreArguments() {
            *reinterpret_cast<uint32_t*>(base + at) = state;
            *reinterpret_cast<uint32_t*>(base + at + 4) = elapsed;
        }
    } restore{base, stack + 4, state, elapsed_bits};
    for (unsigned i = 0; i < steps; ++i) {
        ctx = entry;
        // The original reuses both argument slots for camera temporaries. A catch-up tick must
        // reconstruct its call frame, not inherit the previous tick's overwritten arguments.
        X86_MEM32(stack + 4) = state;
        X86_MEM32(stack + 8) = std::bit_cast<uint32_t>(hulk::SimulationClock::kStep);
        WORLD_UPDATE_ORIGINAL(ctx, base);
    }
}
