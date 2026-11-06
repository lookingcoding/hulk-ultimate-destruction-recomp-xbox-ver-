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
        remainder_ += std::min(double(elapsed), double(kStep) * 4);
        const unsigned steps = std::min(4u, unsigned((remainder_ + 1e-8) / double(kStep)));
        remainder_ = std::max(0.0, remainder_ - steps * double(kStep));
        return steps;
    }
private:
    double remainder_ = 0;
};
}
