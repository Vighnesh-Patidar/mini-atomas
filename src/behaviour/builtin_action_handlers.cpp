#include "mith/behaviour/builtin_action_handlers.h"

#include "mith/behaviour/action.h"
#include "mith/behaviour/action_type.h"
#include "mith/comms/message.h"
#include "mith/comms/transport.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"
#include "mith/core/world.h"

#include <cstring>

namespace mith {

namespace {

// Tiny LE byte readers for the Action::params payload.
inline std::uint32_t read_u32_le(const std::uint8_t* p) noexcept {
    return  static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16)
         | (static_cast<std::uint32_t>(p[3]) << 24);
}
inline float read_f32_le(const std::uint8_t* p) noexcept {
    const std::uint32_t bits = read_u32_le(p);
    float v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

} // namespace

// ---------- MoveActionHandler -----------------------------------------

MoveActionHandler::MoveActionHandler(World& world) noexcept
    : world_(&world) {}

SystemDescriptor MoveActionHandler::describe() const {
    return SystemDescriptor{
        /*name=*/             "MoveActionHandler",
        /*reads_components=*/ { component_id<ActionQueueComponent>() },
        /*writes_components=*/{ component_id<VelocityComponent>()    },
        /*reads_resources=*/  {},
        /*writes_resources=*/ {},
    };
}

void MoveActionHandler::tick(EntityRegistry& registry,
                              const SwarmContext& /*ctx*/,
                              float /*delta_time*/) {
    const EntityID self = registry.self_id();
    const auto& aq   = registry.get<ActionQueueComponent>(self);
    auto&       vel  = registry.get<VelocityComponent>(self);

    for (std::uint8_t i = 0; i < aq.validated_count; ++i) {
        const Action& a = aq.validated[i];
        if (a.type != actions::MOVE) continue;
        vel.vx = read_f32_le(a.params.data() + 0);
        vel.vy = read_f32_le(a.params.data() + 4);
        vel.vz = read_f32_le(a.params.data() + 8);
        ++handled_;
    }
}

// ---------- TransmitActionHandler -------------------------------------

TransmitActionHandler::TransmitActionHandler(World& world) noexcept
    : world_(&world)
    , message_transport_(world.message_transport()) {}

SystemDescriptor TransmitActionHandler::describe() const {
    return SystemDescriptor{
        /*name=*/             "TransmitActionHandler",
        /*reads_components=*/ { component_id<ActionQueueComponent>() },
        /*writes_components=*/{},
        /*reads_resources=*/  {},
        /*writes_resources=*/ { ResourceID::TransportTx },
    };
}

void TransmitActionHandler::tick(EntityRegistry& registry,
                                  const SwarmContext& ctx,
                                  float /*delta_time*/) {
    const EntityID self = registry.self_id();
    const auto& aq      = registry.get<ActionQueueComponent>(self);

    if (!message_transport_ || !message_transport_->supports_messages()) {
        return;
    }
    const HierarchicalID self_hid = world_ ? world_->identity() : HierarchicalID{};

    for (std::uint8_t i = 0; i < aq.validated_count; ++i) {
        const Action& a = aq.validated[i];
        if (a.type != actions::TRANSMIT) continue;

        Message m;
        m.sender      = self_hid;
        m.recipient   = a.target;     // nil unit_id → broadcast (see §7.5)
        m.type        = read_u32_le(a.params.data() + 0);
        m.seq         = read_u32_le(a.params.data() + 4);
        m.timestamp_s = world_ ? world_->synced_time_s() : ctx.elapsed_time_s;
        // Remaining params bytes are the user-defined payload — bytes 8..PARAMS_SIZE
        // map to message payload 0..PARAMS_SIZE-8.
        constexpr std::size_t HEADER = 8;
        const std::size_t copy =
            (Action::PARAMS_SIZE > HEADER) ? (Action::PARAMS_SIZE - HEADER) : 0u;
        if (copy > 0 && copy <= Message::PAYLOAD_SIZE) {
            std::memcpy(m.payload.data(), a.params.data() + HEADER, copy);
        }

        message_transport_->send_message(m);
        ++handled_;
    }
}

// ---------- HoverActionHandler ----------------------------------------

HoverActionHandler::HoverActionHandler(World& world) noexcept
    : world_(&world) {}

SystemDescriptor HoverActionHandler::describe() const {
    return SystemDescriptor{
        /*name=*/             "HoverActionHandler",
        /*reads_components=*/ { component_id<ActionQueueComponent>() },
        /*writes_components=*/{},
        /*reads_resources=*/  {},
        /*writes_resources=*/ {},
    };
}

void HoverActionHandler::tick(EntityRegistry& registry,
                               const SwarmContext& /*ctx*/,
                               float /*delta_time*/) {
    const EntityID self = registry.self_id();
    const auto& aq      = registry.get<ActionQueueComponent>(self);

    for (std::uint8_t i = 0; i < aq.validated_count; ++i) {
        const Action& a = aq.validated[i];
        if (a.type == actions::IDLE || a.type == actions::HOVER) {
            ++handled_;
        }
    }
}

} // namespace mith
