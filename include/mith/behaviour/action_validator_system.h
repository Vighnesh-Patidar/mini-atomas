#pragma once

// ActionValidatorSystem — see ARCHITECTURE.md §6.4
//
// Drains ActionQueueComponent::queue each tick. For each action:
//   - if PermissionMaskComponent::allows(action.type) → copy into
//     ActionQueueComponent::validated[] for handler systems to consume.
//   - else                                         → bump
//     permission_rejections_total + last_rejection_tick + emit a
//     `action_rejected` WARN trace event.
//
// Validated-buffer is overwritten in place each tick — handlers must
// run AFTER the validator (the §5.1 hazard graph already orders this:
// validator writes ActionQueueComponent; handlers read it; the DAG puts
// the writer before all readers within a tick).

#include "mith/core/system.h"

#include <cstdint>

namespace mith {

class World;
class TraceSink;

class ActionValidatorSystem : public System {
public:
    explicit ActionValidatorSystem(World& world) noexcept;

    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

private:
    World*     world_;
    TraceSink* sink_;
};

} // namespace mith
