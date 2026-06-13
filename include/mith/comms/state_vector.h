#pragma once

// StateVector — see ARCHITECTURE.md §7.2
//
// Beacon payload. Fixed schema, compact (~64 bytes), serialisable to a
// single UDP datagram or radio frame. Periodically broadcast by
// BeaconSystem (§5.3); deserialised on receipt and merged into the
// recipient's NeighbourTable (§7.4) as a fresh observation.
//
// Field set is deliberately narrow — only what neighbours need to model
// each other. ActionQueue, CommBuffer, PermissionMask, IdentityKey are
// NOT shipped over beacons; they're not part of "what a neighbour knows
// about you."

#include "mith/core/builtin_components.h"
#include "mith/identity/hierarchical_id.h"

#include <cstdint>

namespace mith {

struct StateVector {
    HierarchicalID          id;
    PositionComponent       position;
    VelocityComponent       velocity;
    HealthComponent         health;
    RoleComponent           role;
    BehaviourStateComponent state;
    std::uint32_t           tick = 0;     // sender's tick at emit time
};

} // namespace mith
