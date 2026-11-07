#pragma once
#include <cstdint>

namespace hulk {
struct SimulationTick { uint32_t clock; float elapsed; };
namespace detail { inline thread_local const SimulationTick* current_tick = nullptr; }
inline const SimulationTick* CurrentSimulationTick() { return detail::current_tick; }

// Each scope lives on its native C++ stack. The fiber bridge suspends the thread-local binding
// while switching away, and restores it when that exact continuation resumes.
class SimulationTickScope {
public:
    SimulationTickScope(uint32_t clock, float elapsed) : tick_{clock, elapsed}, previous_(detail::current_tick) {
        detail::current_tick = &tick_;
    }
    ~SimulationTickScope() { detail::current_tick = previous_; }
    SimulationTickScope(const SimulationTickScope&) = delete;
    SimulationTickScope& operator=(const SimulationTickScope&) = delete;
private:
    SimulationTick tick_;
    const SimulationTick* previous_;
};

class SuspendSimulationTick {
public:
    SuspendSimulationTick() : previous_(detail::current_tick) { detail::current_tick = nullptr; }
    ~SuspendSimulationTick() { detail::current_tick = previous_; }
    SuspendSimulationTick(const SuspendSimulationTick&) = delete;
    SuspendSimulationTick& operator=(const SuspendSimulationTick&) = delete;
private:
    const SimulationTick* previous_;
};
}
