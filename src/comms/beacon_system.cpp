#include "mith/comms/beacon_system.h"

#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/comms/transport.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"
#include "mith/core/world.h"

#include <utility>
#include <vector>

namespace mith {

BeaconSystem::BeaconSystem(World& world) noexcept
    : neighbour_table_(&world.neighbour_table())
    , transport_(world.transport())
    , beacon_period_s_(world.config().beacon_rate_hz > 0.0f
                        ? 1.0f / world.config().beacon_rate_hz
                        : 0.0f)
    , neighbour_timeout_s_(world.config().neighbour_timeout_s) {}

SystemDescriptor BeaconSystem::describe() const {
    return SystemDescriptor{
        /*name=*/             "BeaconSystem",
        /*reads_components=*/ {
            component_id<IdentityComponent>(),
            component_id<PositionComponent>(),
            component_id<VelocityComponent>(),
            component_id<HealthComponent>(),
            component_id<RoleComponent>(),
            component_id<BehaviourStateComponent>(),
        },
        /*writes_components=*/{
            component_id<CommBufferComponent>(),
        },
        /*reads_resources=*/  {ResourceID::TransportRx},
        /*writes_resources=*/ {ResourceID::NeighbourTable, ResourceID::TransportTx},
    };
}

void BeaconSystem::tick(EntityRegistry& registry,
                        const SwarmContext& ctx,
                        float delta_time) {
    const EntityID self = registry.self_id();

    // Always age out — independent of whether we have a transport.
    if (neighbour_table_) {
        neighbour_table_->age_out(ctx.elapsed_time_s, neighbour_timeout_s_);
    }

    if (!transport_) return;   // no transport — nothing more to do this tick

    // 1. Build StateVector from the self entity. All built-in hot
    //    components are guaranteed emplaced after World::init().
    StateVector sv;
    sv.id        = registry.get<IdentityComponent>(self).id;
    sv.position  = registry.get<PositionComponent>(self);
    sv.velocity  = registry.get<VelocityComponent>(self);
    sv.health    = registry.get<HealthComponent>(self);
    sv.role      = registry.get<RoleComponent>(self);
    sv.state     = registry.get<BehaviourStateComponent>(self);
    sv.tick      = static_cast<std::uint32_t>(ctx.tick_count);

    // 2. Send if the beacon period has elapsed.
    time_since_last_beacon_s_ += delta_time;
    if (beacon_period_s_ > 0.0f && time_since_last_beacon_s_ >= beacon_period_s_) {
        transport_->send_beacon(sv);
        time_since_last_beacon_s_ = 0.0f;
    }

    // 3. Drain inbound traffic.
    std::vector<StateVector> beacons;
    std::vector<Message>     messages;
    transport_->poll(beacons, messages);

    // 4. Upsert received beacons (skip the unlikely echo of our own ID).
    for (const auto& b : beacons) {
        if (b.id == sv.id) continue;
        neighbour_table_->upsert(b, ctx.elapsed_time_s);
    }

    // 5. Push messages into the local CommBufferComponent queue. Overflow
    //    follows the DropOldest policy declared on the queue (§4.4) —
    //    push() returns true on accept, false on (impossible-here) reject.
    auto& cb = registry.get<CommBufferComponent>(self);
    for (auto& m : messages) {
        cb.queue.push(std::move(m));
    }
}

} // namespace mith
