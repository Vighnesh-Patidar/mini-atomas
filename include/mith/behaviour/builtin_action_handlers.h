#pragma once

// Built-in action-handler systems — see ARCHITECTURE.md §6.4
//
// Each handler reads ActionQueueComponent::validated[0..validated_count),
// filters by its ActionTypeID, and applies its declared writes. Handlers
// writing disjoint components run in parallel under the §5.1 hazard
// graph — MoveActionHandler (writes VelocityComponent) and
// TransmitActionHandler (writes the TransportTx resource) overlap with
// each other, both serialised against ActionValidatorSystem.
//
// HoverActionHandler is a no-op writer; it exists so that hover/idle
// actions cleanly drain from the validated buffer (a tick worth of
// actions that no other handler claimed would otherwise be invisible
// to observability — having a handler that "consumes" them lets the
// trace event count match).

#include "mith/api_stability.h"
#include "mith/core/system.h"

namespace mith {

class World;
class MessageTransport;

// MOVE: writes VelocityComponent (target velocity vector — KinematicsSystem
// integrates into PositionComponent the next tick).
// Action::params layout (little-endian): vx (f32), vy (f32), vz (f32).
class MITH_STABLE_API MoveActionHandler : public System {
public:
    explicit MoveActionHandler(World& world) noexcept;
    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

    std::uint64_t handled() const noexcept { return handled_; }

private:
    World*        world_;
    std::uint64_t handled_ = 0;
};

// TRANSMIT: enqueues a Message on World::message_transport.
// Action::params layout: type (u32 LE), seq (u32 LE), then up to
// Message::PAYLOAD_SIZE - 8 bytes of payload (mission-defined).
// Action::target is used as Message::recipient (broadcast if nil).
class MITH_STABLE_API TransmitActionHandler : public System {
public:
    explicit TransmitActionHandler(World& world) noexcept;
    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

    std::uint64_t handled() const noexcept { return handled_; }

private:
    World*            world_;
    MessageTransport* message_transport_;
    std::uint64_t     handled_ = 0;
};

// IDLE / HOVER: both action types are claimed by this single handler.
// No declared writes; just bumps handled_ for observability.
class MITH_STABLE_API HoverActionHandler : public System {
public:
    explicit HoverActionHandler(World& world) noexcept;
    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

    std::uint64_t handled() const noexcept { return handled_; }

private:
    World*        world_;
    std::uint64_t handled_ = 0;
};

} // namespace mith
