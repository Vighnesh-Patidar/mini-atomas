#include "mith/systems/discovery_system.h"

#include "mith/comms/neighbour_table.h"
#include "mith/core/system.h"
#include "mith/core/world.h"

namespace mith {

DiscoverySystem::DiscoverySystem(World& world) noexcept
    : DiscoverySystem(world, Params{}) {}

DiscoverySystem::DiscoverySystem(World& world, Params params) noexcept
    : neighbour_table_(&world.neighbour_table())
    , params_(params) {}

SystemDescriptor DiscoverySystem::describe() const {
    return SystemDescriptor{
        /*name=*/             "DiscoverySystem",
        /*reads_components=*/ {},
        /*writes_components=*/{},
        /*reads_resources=*/  {ResourceID::NeighbourTable},
        /*writes_resources=*/ {},
    };
}

void DiscoverySystem::tick(EntityRegistry& /*registry*/,
                            const SwarmContext& /*ctx*/,
                            float delta_time) {
    if (state_ == DiscoveryState::Active) return;

    time_in_bootstrap_s_ += delta_time;

    // Count distinct peers from the NeighbourTable. The table itself
    // already dedupes by HID via upsert, so the entry count IS the
    // distinct-peer count.
    std::uint32_t count = 0;
    if (neighbour_table_) {
        for (auto it = neighbour_table_->begin();
             it != neighbour_table_->end(); ++it) {
            ++count;
        }
    }
    peers_seen_ = count;

    // Quorum reached?
    if (peers_seen_ >= params_.bootstrap_quorum) {
        promote_to_active_();
        return;
    }
    // Timeout fallback — graceful degrade for lone-robot or link-down.
    if (time_in_bootstrap_s_ >= params_.bootstrap_timeout_s) {
        promote_to_active_();
    }
}

void DiscoverySystem::promote_to_active_() noexcept {
    state_ = DiscoveryState::Active;
}

} // namespace mith
