#include "mith/behaviour/action_validator_system.h"

#include "mith/behaviour/action.h"
#include "mith/behaviour/action_type.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"
#include "mith/core/system.h"
#include "mith/core/trace_sink.h"
#include "mith/core/world.h"

namespace mith {

namespace {

// Short string representation of the well-known built-in action types
// for the action_rejected trace event. Custom IDs render as "custom".
const char* action_name(ActionTypeID t) noexcept {
    switch (t) {
        case actions::IDLE:     return "IDLE";
        case actions::MOVE:     return "MOVE";
        case actions::HOVER:    return "HOVER";
        case actions::TRANSMIT: return "TRANSMIT";
        case actions::SCAN:     return "SCAN";
        case actions::REGROUP:  return "REGROUP";
        case actions::FOLLOW:   return "FOLLOW";
        default:                return (t >= actions::CUSTOM) ? "custom" : "reserved";
    }
}

} // namespace

ActionValidatorSystem::ActionValidatorSystem(World& world) noexcept
    : world_(&world)
    , sink_(world.trace_sink()) {}

SystemDescriptor ActionValidatorSystem::describe() const {
    return SystemDescriptor{
        /*name=*/             "ActionValidatorSystem",
        /*reads_components=*/ {
            component_id<PermissionMaskComponent>(),
        },
        /*writes_components=*/{
            component_id<ActionQueueComponent>(),
        },
        /*reads_resources=*/  {},
        /*writes_resources=*/ {},
    };
}

void ActionValidatorSystem::tick(EntityRegistry& registry,
                                  const SwarmContext& ctx,
                                  float /*delta_time*/) {
    const EntityID self = registry.self_id();
    auto& aq   = registry.get<ActionQueueComponent>(self);
    const auto& mask = registry.get<PermissionMaskComponent>(self);

    // Refresh sink — World may have set it after the system was
    // constructed. Cheap.
    sink_ = world_ ? world_->trace_sink() : nullptr;

    // Validated buffer is rebuilt fresh each tick — handlers consume
    // within the same tick, so prior content is stale.
    aq.validated_count = 0;

    while (auto popped = aq.queue.pop()) {
        const Action& a = *popped;
        if (mask.allows(a.type)) {
            if (aq.validated_count < ActionQueueComponent::CAPACITY) {
                aq.validated[aq.validated_count++] = a;
            }
            // Overflow on the validated buffer would be a programmer
            // error: queue and validated share CAPACITY, so a drained
            // queue can never produce more validated entries than the
            // buffer holds.
        } else {
            ++aq.permission_rejections_total;
            aq.last_rejection_tick = ctx.tick_count;
            if (sink_) {
                const TraceField fields[] = {
                    TraceField::str("action_type", action_name(a.type)),
                    TraceField::str("reason",      "permission_mask"),
                    TraceField::u64("tick",        ctx.tick_count),
                };
                sink_->emit(TraceLevel::Warn, "action_rejected",
                            fields, sizeof fields / sizeof *fields);
            }
        }
    }
}

} // namespace mith
