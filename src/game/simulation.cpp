#include <bit>
#include <chrono>
#include <cstdlib>
#include "common/simulation.h"
#include "common/addresses.h"
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
// Five-second update counters (issue #13): which path ran and how many frames were shown, so a
// player's log names the path when updates run far faster than 60 Hz. The window is owned by
// the guest update thread; the presentation counter is atomic.
struct UpdateStats {
    unsigned calls = 0, accumulator_calls = 0, steps = 0, dilated = 0, unpaced = 0;
    unsigned paused6 = 0, paused7 = 0, nonpositive = 0;
    double window = 0;
    uint64_t presents = 0;
} g_stats;
}  // namespace
X86_FUNC(WORLD_UPDATE) {
    static hulk::SimulationClock clock;
    static hulk::FrameGate frames;
    static uint32_t owner = 0;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double real_dt = std::chrono::duration<double>(now - last).count();
    last = now;
    const uint32_t stack = ctx.esp, world = ctx.ecx;
    const uint32_t state = X86_MEM32(ctx.esp + 4), elapsed_bits = X86_MEM32(ctx.esp + 8);
    const float elapsed = std::bit_cast<float>(elapsed_bits);
    // The world timer is owned by the app. Its multiplier is Q16 at +0x38; unlike a
    // wall-clock ratio it is unaffected by scheduling jitter and the guest's stall clamp.
    const uint32_t app = X86_MEM32(hulk::addr::kApp);
    const uint32_t timer = app ? uint32_t(X86_MEM32(app + 0x58)) : 0;
    // Test-only: set the guest multiplier so the guest clock itself scales subsequent frames.
    // HULK_TEST_NO_DILATION_FIX keeps the issue #11 accumulator-only comparison available.
    static const float forced_scale = [] {
        const char* test = std::getenv("HULK_TEST_TIMESCALE");
        return test ? float(std::atof(test)) : 1.0f;
    }();
    static const bool no_fix = std::getenv("HULK_TEST_NO_DILATION_FIX") != nullptr;
    if (timer && forced_scale > 0 && forced_scale < 1) {
        X86_MEM32(timer + 0x38) = uint32_t(std::lround(forced_scale * 65536));
        X86_MEM32(timer + 0x3c) = 0;
    }
    const uint64_t scale = timer ? uint64_t(X86_MEM32(timer + 0x38)) |
                                    (uint64_t(X86_MEM32(timer + 0x3c)) << 32) : 0;
    const bool dilated = !no_fix && hulk::TimeDilated(timer != 0, scale);
    static const bool audit = std::getenv("HULK_TEST_TIMING_AUDIT") != nullptr;
    if (audit && timer && X86_MEM32(state) != 6 && X86_MEM32(state) != 7) {
        static unsigned logged = 0;
        if (logged++ < 100) {
            const uint64_t raw = uint64_t(X86_MEM32(timer + 0x28)) | (uint64_t(X86_MEM32(timer + 0x2c)) << 32);
            const uint64_t game = uint64_t(X86_MEM32(timer + 0x30)) | (uint64_t(X86_MEM32(timer + 0x34)) << 32);
            LOGI("timing audit: elapsed=%.8f real=%.8f scale=%llu bypass=%u raw_ticks=%llu game_ticks=%llu state=%u",
                 elapsed, real_dt, static_cast<unsigned long long>(scale), unsigned(dilated),
                 static_cast<unsigned long long>(raw), static_cast<unsigned long long>(game), uint32_t(X86_MEM32(state)));
        }
    }
    const uint32_t guest_state = X86_MEM32(state);
    // HULK_TEST_NO_FRAME_GATE keeps the accumulator-only comparison for issue #13 available.
    static const bool no_gate = std::getenv("HULK_TEST_NO_FRAME_GATE") != nullptr;
    const bool presented = frames.Presented(hulk::g_frame_swaps.load(std::memory_order_relaxed)) || no_gate;
    auto& stats = g_stats;
    stats.calls++;
    stats.window += real_dt;
    // Include the current call in its path count before reporting the window.
    const auto path = hulk::ClassifyUpdate(guest_state, elapsed, dilated, presented);
    switch (path) {
        case hulk::UpdatePath::Paused6: ++stats.paused6; break;
        case hulk::UpdatePath::Paused7: ++stats.paused7; break;
        case hulk::UpdatePath::Nonpositive: ++stats.nonpositive; break;
        case hulk::UpdatePath::Dilated: ++stats.dilated; break;
        case hulk::UpdatePath::Unpaced: ++stats.unpaced; break;
        case hulk::UpdatePath::Accumulator: ++stats.accumulator_calls; break;
    }
    const auto report = [&] {
        if (stats.window < 5) return;
        const uint64_t presents = gpu::g_visible_presents.load(std::memory_order_relaxed);
        LOGI("world update: %.3f s calls=%u accumulator_calls=%u steps=%u dilated=%u unpaced=%u state6=%u state7=%u elapsed_nonpositive=%u presents=%llu",
             stats.window, stats.calls, stats.accumulator_calls, stats.steps, stats.dilated, stats.unpaced,
             stats.paused6, stats.paused7, stats.nonpositive, static_cast<unsigned long long>(presents - stats.presents));
        if (stats.calls > stats.window * 240.0)
            LOGI("world update high rate: state=%u elapsed=%.9g real_dt=%.9g scale=%llu presented=%u calls_per_second=%.1f",
                 guest_state, elapsed, real_dt, static_cast<unsigned long long>(scale), unsigned(presented),
                 stats.calls / stats.window);
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
    // When a cutscene shot ends, the game holds its renderer (the render manager's byte +0x31, set by
    // NISAnimationAction's end, USA 0x1A8EE0 / EU 0x1A9120) until the next shot starts. After a mission's last
    // shot it stays held while the mission winds up, and the game loop comes straight back each pass with no swap
    // and no app::Pace. The console ran this update on every one of those passes. Run 60 times a second instead,
    // the wind-up of c00m02's escape held a frozen picture for 58 to 109 seconds in tests (issue #13), against
    // about one second when every pass runs it. Passes that swapped no frame run the update once, with the time
    // measured since the previous call plus whatever the clock still held.
    if (path == hulk::UpdatePath::Unpaced) {
        X86_MEM32(stack + 8) = std::bit_cast<uint32_t>(elapsed + clock.Drain());
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
