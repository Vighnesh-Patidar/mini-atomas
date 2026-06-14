#pragma once

// PartitionMergeSystem — see ARCHITECTURE.md §16 v0.3
//
// Detects "partition heal" events by watching for sudden jumps in
// NeighbourTable size. When the table grows by >= partition_heal_threshold
// peers within a short window, the system declares a heal: World's
// merge window is set to merge_window_s seconds. Other systems
// (TaskAllocSystem in particular) consult World::is_merging() to
// temporarily relax hysteresis and let the merged swarm reconverge
// fast.
//
// Depends on the clock-sync foundation (v0.3 prior slice) only
// indirectly — neighbour count is the signal here, not time.
// Symmetric: every robot detects its own heal independently; no
// coordination needed.
//
// This is the §16 v0.3 "TaskAllocSystem partition merge" entry's
// first slice. Cross-robot role-version reconciliation (epoch-leader /
// version-vector) is a later slice — for now, the merge window is
// enough to converge via standard threshold logic on the freshly
// merged peer set.

#include "mith/api_stability.h"
#include "mith/core/system.h"

#include <cstdint>

namespace mith {

class World;
class NeighbourTable;

class MITH_EXPERIMENTAL_API PartitionMergeSystem : public System {
public:
    struct Params {
        // Minimum neighbour-count increase from one tick to the next to
        // count as a heal event.
        std::uint32_t partition_heal_threshold = 2;

        // Length of the merge window opened on a heal event. Set on
        // World; counts down via World::tick().
        float merge_window_s = 2.0f;
    };

    explicit PartitionMergeSystem(World& world) noexcept;
    PartitionMergeSystem(World& world, Params params) noexcept;

    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

    // Observability.
    std::uint64_t heal_events()  const noexcept { return heal_events_; }
    std::uint32_t last_peer_count() const noexcept { return last_peer_count_; }

private:
    World*          world_;
    NeighbourTable* neighbour_table_;
    Params          params_;
    std::uint32_t   last_peer_count_ = 0;
    std::uint64_t   heal_events_     = 0;
};

} // namespace mith
