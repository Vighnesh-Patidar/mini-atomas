#include "mith/sim/sim_clock.h"

#include "mith/core/world.h"

#include <algorithm>

namespace mith::sim {

SimClock::SimClock(float delta_time_s) noexcept
    : delta_time_s_(delta_time_s) {}

void SimClock::register_world(World& w) {
    if (std::find(worlds_.begin(), worlds_.end(), &w) != worlds_.end()) {
        return;   // already registered — idempotent
    }
    worlds_.push_back(&w);
}

void SimClock::unregister_all() noexcept {
    worlds_.clear();
}

void SimClock::advance(std::size_t ticks) {
    for (std::size_t i = 0; i < ticks; ++i) {
        for (World* w : worlds_) {
            w->tick();
        }
        elapsed_time_s_ += delta_time_s_;
        ++tick_count_;
    }
}

float       SimClock::delta_time_s()   const noexcept { return delta_time_s_; }
float       SimClock::elapsed_time_s() const noexcept { return elapsed_time_s_; }
std::size_t SimClock::tick_count()     const noexcept { return tick_count_; }
std::size_t SimClock::world_count()    const noexcept { return worlds_.size(); }

} // namespace mith::sim
