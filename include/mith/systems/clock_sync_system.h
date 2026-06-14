#pragma once

// ClockSyncSystem — see ARCHITECTURE.md §16 v0.3
//
// Decentralised clock synchronisation. Each robot maintains an additive
// `clock_offset_s` on top of its local SwarmContext::elapsed_time_s;
// synced_time = elapsed + offset. ClockSyncSystem nudges the offset
// toward the swarm consensus each tick by averaging the gap between
// every neighbour's last-observed sync_time and our own.
//
// Algorithm — a simple bounded-drift variant of the Gradient Time
// Sync Protocol (GTSP):
//
//   for each NeighbourTable entry n:
//       gap_n = n.sync_time_s - our_synced_time_s
//   mean_gap = mean(gap_n)            // 0 if no neighbours
//   new_offset = old_offset + adjustment_rate * mean_gap
//
// adjustment_rate ∈ (0, 1] controls how aggressively each robot moves
// toward the mean. Smaller → smoother, slower convergence; larger →
// faster but more oscillation. Default 0.1.
//
// Pre-clock-sync foundation only — partition-merge reconciliation
// (epoch-leader / version-vector) is a later v0.3 slice.

#include "mith/core/system.h"

#include <cstdint>

namespace mith {

class World;
class NeighbourTable;

class ClockSyncSystem : public System {
public:
    struct Params {
        // Fraction of the mean offset gap applied per tick. 0.0 disables
        // synchronisation entirely (the system becomes a no-op);
        // 1.0 means "snap to the mean every tick" — useful only for
        // tests that need fast convergence.
        float adjustment_rate = 0.1f;

        // Maximum |offset change| per tick (seconds). Bounds runaway
        // adjustment when a malformed peer reports a wildly skewed
        // sync_time. 0 disables the cap.
        float max_step_s = 1.0f;
    };

    explicit ClockSyncSystem(World& world) noexcept;
    ClockSyncSystem(World& world, Params params) noexcept;

    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

    // Observability.
    std::uint64_t adjustments_applied() const noexcept { return adjustments_; }
    std::uint64_t observations()        const noexcept { return observations_; }

private:
    World*          world_;
    NeighbourTable* neighbour_table_;
    Params          params_;
    std::uint64_t   adjustments_  = 0;
    std::uint64_t   observations_ = 0;
};

} // namespace mith
