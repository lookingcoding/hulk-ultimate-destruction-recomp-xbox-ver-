#pragma once
#include <algorithm>
#include <cmath>

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

// True when the game is running slowed time: its elapsed frame time is well below the real
// time between world updates. A critical move's slow motion scales the game's frame timer
// down (its time-scale multiplier), and the console answered by running the world update
// every rendered frame with the dilated time, so its camera and animations kept moving each
// frame. The 60 Hz accumulator must stand aside while time is dilated, or it would skip most
// rendered frames and turn the cinematic into a slideshow (issue #11). The bound leaves room
// for the two clocks' jitter at full speed; a frame stall makes elapsed relatively larger,
// which reads as not dilated and stays on the accumulator's catch-up path.
inline bool TimeDilated(float elapsed, double real_dt) {
    if (!std::isfinite(elapsed) || elapsed <= 0 || !(real_dt > 0)) return false;
    return elapsed < real_dt * 0.75;
}
}
