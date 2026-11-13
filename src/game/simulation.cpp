#include <bit>
#include <chrono>
#include <cstdlib>
#include "common/simulation.h"
#include "common/addresses.h"
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
    if (forced_scale > 0) {
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
