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
    return 0;
}
