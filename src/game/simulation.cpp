#include <bit>
#include <chrono>
#include <cstdlib>
#include "common/simulation.h"
#include "core/log.h"
#include "gpu/backend.h"
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
// Window is owned by the guest update thread; the presentation counter is atomic.
struct UpdateStats {
    unsigned calls = 0, accumulator_calls = 0, steps = 0, dilated = 0;
    unsigned paused6 = 0, paused7 = 0, nonpositive = 0;
    double window = 0;
    uint64_t presents = 0;
} g_stats;
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
    if (forced_scale > 0 && forced_scale < 1) {
        elapsed = scaled;
        X86_MEM32(ctx.esp + 8) = std::bit_cast<uint32_t>(elapsed);
    }
    const uint32_t guest_state = X86_MEM32(state);
    auto& stats = g_stats;
    stats.calls++;
    stats.window += real_dt;
    // Include the current call in its path count before reporting the window.
    const auto path = hulk::ClassifyUpdate(guest_state, elapsed, dilated);
    switch (path) {
        case hulk::UpdatePath::Paused6: ++stats.paused6; break;
        case hulk::UpdatePath::Paused7: ++stats.paused7; break;
        case hulk::UpdatePath::Nonpositive: ++stats.nonpositive; break;
        case hulk::UpdatePath::Dilated: ++stats.dilated; break;
        case hulk::UpdatePath::Accumulator: ++stats.accumulator_calls; break;
    }
    const auto report = [&] {
        if (stats.window < 5) return;
        const uint64_t presents = gpu::g_visible_presents.load(std::memory_order_relaxed);
        LOGI("world update: %.3f s calls=%u accumulator_calls=%u steps=%u dilated=%u state6=%u state7=%u elapsed_nonpositive=%u presents=%llu",
             stats.window, stats.calls, stats.accumulator_calls, stats.steps, stats.dilated,
             stats.paused6, stats.paused7, stats.nonpositive, static_cast<unsigned long long>(presents - stats.presents));
        if (stats.calls > stats.window * 240.0)
            LOGI("world update high rate: state=%u elapsed=%.9g real_dt=%.9g calls_per_second=%.1f",
                 guest_state, elapsed, real_dt, stats.calls / stats.window);
        stats = {}; stats.presents = presents;
    };
    // Preserve the original paused/frontend branch and discard fractional time at transitions.
    if (owner != world) { clock.Reset(); owner = world; }
    if (path == hulk::UpdatePath::Paused6 || path == hulk::UpdatePath::Paused7 ||
        path == hulk::UpdatePath::Nonpositive) {
        clock.Reset();
        report();
        WORLD_UPDATE_ORIGINAL(ctx, base);
        return;
    }
    // A critical move's slow motion scales the game's frame time down, and the console ran
    // this update every rendered frame with the dilated time, so its camera and animations
    // kept moving each frame. The 60 Hz accumulator would see only a fraction of a tick per
    // frame, skip the update on most frames, and freeze the cinematic's camera and animation
    // to a slideshow while the renderer keeps its full frame rate (issue #11). While time is
    // dilated, run the console's way and keep the accumulator out of it.
    if (path == hulk::UpdatePath::Dilated) {
        clock.Reset();
        report();
        WORLD_UPDATE_ORIGINAL(ctx, base);
        return;
    }
    const unsigned steps = clock.Advance(elapsed);
    stats.steps += steps;
    report();
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
