#pragma once

// EntitySnapshot — see ARCHITECTURE.md §6.2
//
// Read-only snapshot of a single entity's built-in hot components. Built
// by EntityRegistry::snapshot() (§4.3) and consumed by ActionProvider::
// evaluate() (§6.2). Fields are copies — modifying the snapshot does not
// affect the registry.
//
// Components that haven't been emplaced retain their default values
// (PositionComponent defaults to origin, HealthComponent to 100, etc.).
// This lets ActionProviders be written defensively without checking each
// component's presence.

#include "mith/core/builtin_components.h"
#include "mith/core/entity.h"
#include "mith/identity/hierarchical_id.h"

namespace mith {

struct EntitySnapshot {
    EntityID                id = INVALID_ENTITY;
    HierarchicalID          hid;
    PositionComponent       position;
    VelocityComponent       velocity;
    OrientationComponent    orientation;
    HealthComponent         health;
    RoleComponent           role;
    BehaviourStateComponent state;
};

} // namespace mith
