#include <cmath>
#include <cstdio>
#include <limits>
#include "common/simulation.h"

int main() {
    for (unsigned fps : {30u, 60u, 120u, 144u}) {
        hulk::SimulationClock clock;
        unsigned ticks = 0;
        for (unsigned i = 0; i < fps * 120; ++i) ticks += clock.Advance(1.0f / fps);
        // Float input quantization may leave at most one fractional tick after two minutes.
        if (ticks < 7199 || ticks > 7200) { std::printf("%u fps: %u ticks\n", fps, ticks); return 1; }
    }
    hulk::SimulationClock clock;
    unsigned ticks = 0;
    // Alternating frame times with the same total elapsed time must preserve game speed.
    for (unsigned i = 0; i < 600; ++i) { ticks += clock.Advance(0.005f); ticks += clock.Advance(0.015f); }
    if (ticks < 719 || ticks > 720) return 2;
    if (clock.Advance(10.0f) != 4 || clock.Advance(0) != 0) return 3;
    if (clock.Advance(std::numeric_limits<float>::quiet_NaN()) != 0) return 4;
    if (clock.Advance(-1) != 0 || clock.Advance(hulk::SimulationClock::kStep) != 1) return 5;
    // A long stall drops excess time, including any prior fractional tick, and does not
    // leave a catch-up backlog to drain over subsequent frames.
    hulk::SimulationClock stalled;
    if (stalled.Advance(hulk::SimulationClock::kStep * 0.5f) != 0) return 15;
    if (stalled.Advance(30.0f) != 4) return 16;
    if (stalled.Advance(hulk::SimulationClock::kStep * 0.5f) != 0) return 17;
    if (stalled.Advance(hulk::SimulationClock::kStep * 0.5f) != 1) return 18;
    // Normal-speed hitches and jitter cannot trigger a variable-dt update; only the guest's
    // multiplier decides. Missing clocks must not be mistaken for a zero multiplier.
    if (hulk::TimeDilated(false, 0)) return 6;
    if (hulk::TimeDilated(true, 65536)) return 7;
    if (hulk::TimeDilated(true, 131072)) return 8;
    if (!hulk::TimeDilated(true, 65535)) return 9;
    if (!hulk::TimeDilated(true, 6554)) return 10;
    if (!hulk::TimeDilated(true, 0)) return 11;
    hulk::SimulationClock hitch;
    if (hitch.Advance(0.066666f) != 3 || hitch.Advance(0.000001f) != 1) return 12;
    using hulk::UpdatePath;
    if (hulk::ClassifyUpdate(6, 0, true, true) != UpdatePath::Paused6) return 19;
    if (hulk::ClassifyUpdate(7, -1, true, false) != UpdatePath::Paused7) return 20;
    if (hulk::ClassifyUpdate(2, 0, true, false) != UpdatePath::Nonpositive) return 21;
    if (hulk::ClassifyUpdate(2, 0.001f, true, true) != UpdatePath::Dilated) return 22;
    if (hulk::ClassifyUpdate(2, 0.01f, false, true) != UpdatePath::Accumulator) return 23;
    // Issue #13: a pass of the game loop that swapped no frame runs the update itself; slow motion keeps priority.
    if (hulk::ClassifyUpdate(4, 0.0001f, false, false) != UpdatePath::Unpaced) return 24;
    if (hulk::ClassifyUpdate(4, 0.0001f, true, false) != UpdatePath::Dilated) return 25;
    if (hulk::ClassifyUpdate(6, 0.0001f, false, false) != UpdatePath::Paused6) return 26;
    if (hulk::ClassifyUpdate(4, 0, false, false) != UpdatePath::Nonpositive) return 27;
    // The gate compares swap counts between updates: the first update counts as presented, then only a changed
    // count does, including when the counter wraps.
    hulk::FrameGate gate;
    if (!gate.Presented(7)) return 28;
    if (gate.Presented(7) || gate.Presented(7)) return 29;
    if (!gate.Presented(8) || !gate.Presented(10) || gate.Presented(10)) return 30;
    hulk::FrameGate wrapping;
    if (!wrapping.Presented(0xFFFFFFFFu) || !wrapping.Presented(0) || wrapping.Presented(0)) return 31;
    // A presented frame every pass at 144 fps keeps 60 Hz steps; a held renderer (passes with no swap) runs the
    // update every pass, and the clock's held time goes to the first of them, so no time is lost or repeated.
    {
        hulk::SimulationClock paced;
        hulk::FrameGate frames;
        uint32_t swaps = 0;
        unsigned steps = 0, unpaced = 0;
        double simulated = 0;
        const auto pass = [&](float elapsed, bool swapped) {
            if (swapped) ++swaps;
            if (hulk::ClassifyUpdate(4, elapsed, false, frames.Presented(swaps)) == UpdatePath::Unpaced) {
                ++unpaced;
                simulated += double(elapsed) + paced.Drain();
            } else {
                const unsigned n = paced.Advance(elapsed);
                steps += n;
                simulated += n * double(hulk::SimulationClock::kStep);
            }
        };
        for (unsigned i = 0; i < 144 * 10; ++i) pass(1.0f / 144, true);
        if (steps < 599 || steps > 600 || unpaced) { std::printf("144 fps: %u steps %u unpaced\n", steps, unpaced); return 32; }
        // One second with the renderer held: a frame swaps, then 2000 passes of half a millisecond show nothing.
        pass(1.0f / 144, true);
        for (unsigned i = 0; i < 2000; ++i) pass(0.0005f, false);
        if (unpaced != 2000) return 33;
        for (unsigned i = 0; i < 144; ++i) pass(1.0f / 144, true);
        const double real = (144 * 10 + 1 + 144) / 144.0 + 2000 * 0.0005;
        if (simulated > real + 1e-3 || simulated < real - hulk::SimulationClock::kStep) {
            std::printf("simulated %.6f of %.6f s\n", simulated, real);
            return 34;
        }
    }
    hulk::SimulationClock drained;
    if (drained.Advance(hulk::SimulationClock::kStep * 0.75f) != 0) return 35;
    if (std::abs(drained.Drain() - hulk::SimulationClock::kStep * 0.75f) > 1e-6f || drained.Drain() != 0) return 36;
    if (drained.Advance(hulk::SimulationClock::kStep * 0.75f) != 0) return 37;  // nothing left over from before
    return 0;
}
