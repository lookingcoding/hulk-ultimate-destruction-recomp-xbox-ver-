#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace hulk {
// The original world pipeline has per-update collision/pose work as well as dt integration.
// Keep its phases together at 60 Hz; rendering and Windows input polling remain independent.
class SimulationClock {
public:
    static constexpr float kStep = 1.0f / 60.0f;
    void Reset() { remainder_ = 0; }
    unsigned Advance(float elapsed) {
        if (!std::isfinite(elapsed) || elapsed <= 0) { Reset(); return 0; }
        // Match the guest clock's four-tick (1/15 s) stall bound. Never build an unbounded backlog.
        remainder_ = std::min(remainder_ + double(elapsed), double(kStep) * 4);
        const unsigned steps = std::min(4u, unsigned((remainder_ + 1e-8) / double(kStep)));
        remainder_ = std::max(0.0, remainder_ - steps * double(kStep));
        return steps;
    }
    // Time measured but not yet simulated, handed to an update that runs outside the clock so it is not lost.
    float Drain() {
        const double remainder = remainder_;
        remainder_ = 0;
        return float(remainder);
    }
private:
    double remainder_ = 0;
};

// The game's frame swaps, counted by the frame swap hook (hooks.cpp) on the guest's game thread.
inline std::atomic<uint32_t> g_frame_swaps{0};

// Whether the game swapped a frame between two world updates. The console ran the world update once per pass of
// its game loop; a pass that shows a frame is paced by the renderer (app::Pace), a pass that shows none comes
// straight back. The first update has nothing to compare with and counts as presented.
class FrameGate {
public:
    bool Presented(uint32_t swaps) {
        const bool presented = !primed_ || swaps != last_;
        primed_ = true;
        last_ = swaps;
        return presented;
    }
private:
    uint32_t last_ = 0;
    bool primed_ = false;
};

// Classify in the same priority order as the guest hook's bypass branches. Only passes that presented a frame go
// through the 60 Hz accumulator (issue #10); a pass that presented nothing runs the update as the console did
// (issue #13: the game holds its renderer after a cutscene shot and spins its loop until the scene moves on).
enum class UpdatePath { Paused6, Paused7, Nonpositive, Dilated, Unpaced, Accumulator };
inline UpdatePath ClassifyUpdate(uint32_t state, float elapsed, bool dilated, bool presented) {
    if (state == 6) return UpdatePath::Paused6;
    if (state == 7) return UpdatePath::Paused7;
    if (elapsed <= 0) return UpdatePath::Nonpositive;
    if (dilated) return UpdatePath::Dilated;
    if (!presented) return UpdatePath::Unpaced;
    return UpdatePath::Accumulator;
}

// The guest timer stores its multiplier as an unsigned Q16 fixed-point value.
// A missing timer is not evidence of slow motion.
inline bool TimeDilated(bool timer_present, uint64_t scale) {
    return timer_present && scale < 65536;
}
}
