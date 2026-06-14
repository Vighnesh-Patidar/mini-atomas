#pragma once

// SimBus + SimTransport — see ARCHITECTURE.md §9.1
//
// In-process loopback bus connecting multiple World instances for sim
// scenarios (10-robot flocking demo, fault-injection tests). Each World
// owns a SimTransport that forwards outbound beacons / messages to the
// SimBus, which routes them to other participants subject to:
//
//   - Range limiting via SimBusConfig::comm_range_m (v0.1 — enforced).
//   - Packet loss via SimBusConfig::packet_loss_pct (v0.1 — RESERVED;
//     enforcement lands in a follow-up with a seeded RNG).
//   - Latency via SimBusConfig::latency_ms (v0.1 — RESERVED; enforcement
//     lands in a follow-up with a scheduled-delivery queue).
//
// Lifetime model:
//   - SimBus owns its SimClock and the participant registry.
//   - SimBus::create_world(swarm, hid) returns a fully-wired
//     std::unique_ptr<World> — the caller owns the World, SimBus holds
//     non-owning references for routing. The caller must keep the World
//     alive at least as long as the SimBus references it.
//
// Determinism: comm_range_m is computed in a deterministic order
// (registration order). Packet loss + latency, once implemented, will
// use a seeded RNG; the seed is part of SimBusConfig (added later).

#include "mith/comms/transport.h"
#include "mith/core/builtin_components.h"   // PositionComponent for the provider
#include "mith/core/world.h"
#include "mith/identity/hierarchical_id.h"
#include "mith/sim/sim_clock.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace mith::sim {

using PositionProvider = std::function<PositionComponent()>;

struct SimBusConfig {
    float tick_rate_hz    = 20.0f;
    float comm_range_m    = 50.0f;
    float packet_loss_pct = 0.0f;   // RESERVED — not yet enforced
    float latency_ms      = 0.0f;   // RESERVED — not yet enforced
};

class SimTransport;   // fwd — concrete impl below

class SimBus {
public:
    explicit SimBus(SimBusConfig config = {});

    // Build a WorldConfig with sim-appropriate defaults — Sequential
    // scheduler (per §9.2 determinism), tick_rate_hz matching the bus,
    // and the supplied swarm_id.
    WorldConfig make_world_config(SwarmID swarm_id) const noexcept;

    // Construct a World fully wired into this bus: SimTransport bound
    // to the bus, position provider tied to the World's
    // PositionComponent, registered with the SimClock for ticking.
    //
    // World::init() generates its own HID — call w->identity() after
    // w->init() to learn what identity routing uses.
    //
    // Caller owns the returned World; SimBus holds non-owning refs for
    // routing. Caller must keep the World alive while the bus references
    // it.
    std::unique_ptr<World> create_world(SwarmID swarm_id);

    // Tick the SimClock (= tick all created Worlds N times round-robin).
    void advance(std::size_t ticks = 1);

    SimClock&       clock()       noexcept;
    const SimClock& clock() const noexcept;

    std::size_t      participant_count() const noexcept;
    const SimBusConfig& config()         const noexcept;

private:
    friend class SimTransport;

    // Called by SimTransport::send_beacon / send_message. Applies the
    // range filter and pushes packets into target participants' inboxes.
    void deliver_beacon_(std::size_t from_idx, const StateVector& sv);
    void deliver_message_(std::size_t from_idx, const Message& msg);

    // Called by SimTransport::poll_beacons / poll_messages. Drains the
    // per-channel inbox into the caller's vector.
    void drain_inbox_beacons_(std::size_t idx, std::vector<StateVector>& out);
    void drain_inbox_messages_(std::size_t idx, std::vector<Message>& out);

    struct Participant {
        World*                    world;   // not owned; init() generates the HID
        PositionProvider          pos_fn;
        std::vector<StateVector>  inbound_beacons;
        std::vector<Message>      inbound_messages;
    };

    SimBusConfig              config_;
    SimClock                  clock_;
    std::vector<Participant>  participants_;
};

// Concrete TransportLayer for sim. Constructed only by SimBus —
// users get one indirectly via SimBus::create_world().
class SimTransport : public TransportLayer {
public:
    bool send_beacon(const StateVector& sv) override;
    bool send_message(const Message& msg) override;
    void poll_beacons(std::vector<StateVector>& out)  override;
    void poll_messages(std::vector<Message>& out)     override;
    bool is_healthy() const override;

private:
    friend class SimBus;

    SimTransport(SimBus& bus, std::size_t participant_id) noexcept
        : bus_(&bus), participant_id_(participant_id) {}

    SimBus*     bus_;
    std::size_t participant_id_;
};

} // namespace mith::sim
