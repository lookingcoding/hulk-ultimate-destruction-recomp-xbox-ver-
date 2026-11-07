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

struct SimulationSteps { unsigned count = 0; float elapsed = 0; };
class WorldSimulationClock {
public:
    void Reset() { active_.Reset(); paused_.Reset(); }
    SimulationSteps Advance(float elapsed, float wall_elapsed) {
        if (!std::isfinite(elapsed) || elapsed < 0) { Reset(); return {}; }
        // Paused cinematics still need controller/camera housekeeping. Schedule those calls by
        // real time at 60 Hz, with ZERO simulation delta, rather than per render or not at all.
        // Keep the two fractional budgets across mode changes. Otherwise alternating sub-tick
        // active/zero frames discard every slice above 60 fps and can stop world work entirely.
        if (elapsed == 0) return {paused_.Advance(wall_elapsed), 0};
        return {active_.Advance(elapsed), SimulationClock::kStep};
    }
private:
    SimulationClock active_, paused_;
};
}
