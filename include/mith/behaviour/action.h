#pragma once

// Action — see ARCHITECTURE.md §6.1
//
// A single intended operation produced by an ActionProvider (§6.2) per tick.
// Pushed into ActionQueueComponent (§4.4); drained by ActionValidatorSystem
// (§6.4) which checks the type against PermissionMaskComponent (§4.4, §13.2)
// and marks rejected actions; surviving actions are dispatched to typed
// handler systems (§6.4 — MoveActionHandler, TransmitActionHandler, etc.).
//
// POD-style: trivially copyable, noexcept-default-constructible, no
// allocations. Defaults to actions::IDLE so a default-constructed Action
// is harmless if it ever reaches a handler.
//
// `modifies` is documented in §6.1 as a write mask for validation. Under
// the §6.4 handler-split model the *handler's* SystemDescriptor declares
// its writes statically, so v0.1 does not consult Action::modifies at the
// runtime. It is kept here for forward compatibility and as documentation
// of intent — populate it if your action provider wants to communicate
// expected writes to other tooling.

#include "mith/api_stability.h"
#include "mith/behaviour/action_type.h"
#include "mith/core/component.h"            // ComponentTypeID
#include "mith/identity/hierarchical_id.h"  // HierarchicalID

#include <array>
#include <cstdint>

namespace mith {

struct MITH_STABLE_API Action {
    static constexpr std::size_t MAX_MODIFIES   = 4;
    static constexpr std::size_t PARAMS_SIZE    = 64;

    ActionTypeID                                  type           = actions::IDLE;
    float                                         priority       = 0.0f;
    std::array<ComponentTypeID, MAX_MODIFIES>     modifies{};      // up to MAX_MODIFIES entries
    std::uint8_t                                  modifies_count = 0;
    std::array<std::uint8_t, PARAMS_SIZE>         params{};        // user-defined payload
    HierarchicalID                                target{};        // {0, nil UUID} = no target
};

} // namespace mith
