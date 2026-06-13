#pragma once

// SimClock — see ARCHITECTURE.md §9.2
//
// Virtual clock for in-process simulation. Multiple World instances
// register with one SimClock; advance(N) ticks each registered World N
// times in round-robin order. The per-tick delta_time_s is the clock's
// configured value — Worlds should be configured (via SimBus::
// make_world_config) with a matching tick_rate_hz.
//
// Determinism contract (§5.2 / §9.2):
//   SimClock produces bit-identical traces across runs ONLY when every
//   participating World uses SchedulerMode::Sequential. SimBus::
//   make_world_config sets Sequential by default; users can opt out
//   for perf-benchmark sims at the cost of determinism.
//
// Not thread-safe. Lifetime: registered Worlds must outlive the
// SimClock, or the caller must unregister_all() before destruction.

#include <cstddef>
#include <vector>

namespace mith {
class World;   // fwd

namespace sim {

class SimClock {
public:
    explicit SimClock(float delta_time_s = 0.05f) noexcept;

    // Register a World. Round-robin order is the registration order.
    // No-op if w is already registered.
    void register_world(World& w);

    // Forget all registered Worlds. Counters preserved.
    void unregister_all() noexcept;

    // Tick every registered World N times, round-robin. Each World's
    // tick() is called once per iteration; the clock's tick_count and
    // elapsed_time_s advance once per iteration (NOT once per World per
    // iteration).
    void advance(std::size_t ticks = 1);

    float       delta_time_s()   const noexcept;
    float       elapsed_time_s() const noexcept;
    std::size_t tick_count()     const noexcept;
    std::size_t world_count()    const noexcept;

private:
    float                delta_time_s_;
    float                elapsed_time_s_ = 0.0f;
    std::size_t          tick_count_     = 0;
    std::vector<World*>  worlds_;
};

} // namespace sim
} // namespace mith
