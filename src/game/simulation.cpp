#include <bit>
#include <chrono>
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
namespace {
// Test-only instrumentation for issue #11's A/B run: how many times the original world update
// actually ran, logged every five seconds while HULK_TEST_TIMESCALE forces a slow motion.
unsigned g_updates = 0;
double g_update_window = 0;
}  // namespace
X86_FUNC(WORLD_UPDATE) {
    static hulk::SimulationClock clock;
    static uint32_t owner = 0;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double real_dt = std::chrono::duration<double>(now - last).count();
    last = now;
    const uint32_t stack = ctx.esp, world = ctx.ecx;
    const uint32_t state = X86_MEM32(ctx.esp + 4), elapsed_bits = X86_MEM32(ctx.esp + 8);
    float elapsed = std::bit_cast<float>(elapsed_bits);
    // Test-only: HULK_TEST_TIMESCALE forces the game's slow motion on every frame, so the
    // dilated path can be exercised without a critical move (issue #11's A/B harness). The
    // guest argument slot is scaled before anything reads it, exactly as the game's own
    // time-scale multiplier shrinks it. HULK_TEST_NO_DILATION_FIX reproduces v0.4.5's
    // behaviour (the dilated frames stay on the 60 Hz accumulator) for the comparison.
    static const float forced_scale = [] {
        const char* test = std::getenv("HULK_TEST_TIMESCALE");
        return test ? float(std::atof(test)) : 1.0f;
    }();
    static const bool no_fix = std::getenv("HULK_TEST_NO_DILATION_FIX") != nullptr;
    // The forced scale stands in for the game's own slow-motion multiplier, which shrinks the
    // elapsed before this hook sees it; the dilation check must see it shrunk.
    const float scaled = forced_scale > 0 && forced_scale < 1 ? elapsed * forced_scale : elapsed;
    const bool dilated = !no_fix && hulk::TimeDilated(scaled, real_dt);
    if (forced_scale > 0) {
        if (forced_scale < 1) {
            elapsed = scaled;
            X86_MEM32(ctx.esp + 8) = std::bit_cast<uint32_t>(elapsed);
        }
        g_update_window += real_dt;
        if (g_update_window >= 5) {
            LOGI("world update test: %u updates in %.1f s at %.2fx time scale%s", g_updates, g_update_window,
                 forced_scale, no_fix ? ", the accumulator's way" : "");
            g_updates = 0, g_update_window = 0;
        }
    }
    // Preserve the original paused/frontend branch and discard fractional time at transitions.
    if (owner != world) { clock.Reset(); owner = world; }
    if (X86_MEM32(state) == 6 || X86_MEM32(state) == 7 || elapsed <= 0) {
        clock.Reset();
        g_updates++;
        WORLD_UPDATE_ORIGINAL(ctx, base);
        return;
    }
    // A critical move's slow motion scales the game's frame time down, and the console ran
    // this update every rendered frame with the dilated time, so its camera and animations
    // kept moving each frame. The 60 Hz accumulator would see only a fraction of a tick per
    // frame, skip the update on most frames, and freeze the cinematic's camera and animation
    // to a slideshow while the renderer keeps its full frame rate (issue #11). While time is
    // dilated, run the console's way and keep the accumulator out of it.
    if (dilated) {
        clock.Reset();
        g_updates++;
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
        g_updates++;
        WORLD_UPDATE_ORIGINAL(ctx, base);
    }
}
