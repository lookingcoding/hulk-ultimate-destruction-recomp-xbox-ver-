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
    if (hulk::ClassifyUpdate(6, 0, true) != UpdatePath::Paused6) return 19;
    if (hulk::ClassifyUpdate(7, -1, true) != UpdatePath::Paused7) return 20;
    if (hulk::ClassifyUpdate(2, 0, true) != UpdatePath::Nonpositive) return 21;
    if (hulk::ClassifyUpdate(2, 0.001f, true) != UpdatePath::Dilated) return 22;
    if (hulk::ClassifyUpdate(2, 0.01f, false) != UpdatePath::Accumulator) return 23;
    return 0;
}
