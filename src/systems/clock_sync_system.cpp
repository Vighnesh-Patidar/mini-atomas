#include "mith/systems/clock_sync_system.h"

#include "mith/comms/neighbour_table.h"
#include "mith/core/system.h"
#include "mith/core/world.h"

#include <algorithm>
#include <cmath>

namespace mith {

ClockSyncSystem::ClockSyncSystem(World& world) noexcept
    : ClockSyncSystem(world, Params{}) {}

ClockSyncSystem::ClockSyncSystem(World& world, Params params) noexcept
    : world_(&world)
    , neighbour_table_(&world.neighbour_table())
    , params_(params) {}

SystemDescriptor ClockSyncSystem::describe() const {
    return SystemDescriptor{
        /*name=*/             "ClockSyncSystem",
        /*reads_components=*/ {},
        /*writes_components=*/{},
        /*reads_resources=*/  {ResourceID::NeighbourTable},
        /*writes_resources=*/ {},
    };
}

void ClockSyncSystem::tick(EntityRegistry& /*registry*/,
                            const SwarmContext& /*ctx*/,
                            float /*delta_time*/) {
    if (!world_ || !neighbour_table_) return;
    if (params_.adjustment_rate <= 0.0f) return;

    const float my_sync = world_->synced_time_s();

    float        sum  = 0.0f;
    std::uint32_t n   = 0;
    for (auto it = neighbour_table_->begin();
         it != neighbour_table_->end(); ++it) {
        sum += it->sync_time_s - my_sync;
        ++n;
    }
    if (n == 0) return;   // alone in the swarm — nothing to converge toward
    observations_ += n;

    const float mean_gap = sum / static_cast<float>(n);
    float       step     = params_.adjustment_rate * mean_gap;
    if (params_.max_step_s > 0.0f) {
        step = std::clamp(step, -params_.max_step_s, params_.max_step_s);
    }

    if (std::fabs(step) > 0.0f) {
        world_->set_clock_offset_s(world_->clock_offset_s() + step);
        ++adjustments_;
    }
}

} // namespace mith
