#pragma once
#include <algorithm>
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
private:
    double remainder_ = 0;
};

// Classify in the same priority order as the guest hook's bypass branches.
enum class UpdatePath { Paused6, Paused7, Nonpositive, Dilated, Accumulator };
inline UpdatePath ClassifyUpdate(uint32_t state, float elapsed, bool dilated) {
    if (state == 6) return UpdatePath::Paused6;
    if (state == 7) return UpdatePath::Paused7;
    if (elapsed <= 0) return UpdatePath::Nonpositive;
    if (dilated) return UpdatePath::Dilated;
    return UpdatePath::Accumulator;
}

// The guest timer stores its multiplier as an unsigned Q16 fixed-point value.
// A missing timer is not evidence of slow motion.
inline bool TimeDilated(bool timer_present, uint64_t scale) {
    return timer_present && scale < 65536;
}
}
