#include "mith/systems/partition_merge_system.h"

#include "mith/comms/neighbour_table.h"
#include "mith/core/system.h"
#include "mith/core/world.h"

namespace mith {

PartitionMergeSystem::PartitionMergeSystem(World& world) noexcept
    : PartitionMergeSystem(world, Params{}) {}

PartitionMergeSystem::PartitionMergeSystem(World& world, Params params) noexcept
    : world_(&world)
    , neighbour_table_(&world.neighbour_table())
    , params_(params) {}

SystemDescriptor PartitionMergeSystem::describe() const {
    return SystemDescriptor{
        /*name=*/             "PartitionMergeSystem",
        /*reads_components=*/ {},
        /*writes_components=*/{},
        /*reads_resources=*/  {ResourceID::NeighbourTable},
        /*writes_resources=*/ {},
    };
}

void PartitionMergeSystem::tick(EntityRegistry& /*registry*/,
                                 const SwarmContext& /*ctx*/,
                                 float /*delta_time*/) {
    if (!world_ || !neighbour_table_) return;

    std::uint32_t count = 0;
    for (auto it = neighbour_table_->begin();
         it != neighbour_table_->end(); ++it) {
        ++count;
    }

    if (count > last_peer_count_
        && (count - last_peer_count_) >= params_.partition_heal_threshold) {
        // Heal detected — open the merge window. set_merge_window_s
        // overrides any prior remaining time; treats nested heals as
        // resetting the clock (worst case: a long-running merge window
        // that ultimately exits when no further heals fire).
        world_->set_merge_window_s(params_.merge_window_s);
        ++heal_events_;
    }
    last_peer_count_ = count;
}

} // namespace mith
