#include "mith/sim/sim_bus.h"

#include "mith/core/builtin_components.h"

#include <utility>

namespace mith::sim {

// ------------------------------------------------------------------------
// SimTransport
// ------------------------------------------------------------------------

bool SimTransport::send_beacon(const StateVector& sv) {
    bus_->deliver_beacon_(participant_id_, sv);
    return true;
}

bool SimTransport::send_message(const Message& msg) {
    bus_->deliver_message_(participant_id_, msg);
    return true;
}

void SimTransport::poll_beacons(std::vector<StateVector>& out) {
    bus_->drain_inbox_beacons_(participant_id_, out);
}

void SimTransport::poll_messages(std::vector<Message>& out) {
    bus_->drain_inbox_messages_(participant_id_, out);
}

bool SimTransport::is_healthy() const {
    return true;   // sim is always healthy in v0.1
}

// ------------------------------------------------------------------------
// SimBus
// ------------------------------------------------------------------------

SimBus::SimBus(SimBusConfig config)
    : config_(config)
    , clock_(1.0f / config.tick_rate_hz) {}

WorldConfig SimBus::make_world_config(SwarmID swarm_id) const noexcept {
    WorldConfig cfg;
    cfg.swarm_id        = swarm_id;
    cfg.tick_rate_hz    = config_.tick_rate_hz;
    cfg.scheduler_mode  = SchedulerMode::Sequential;   // determinism (§9.2)
    return cfg;
}

std::unique_ptr<World> SimBus::create_world(SwarmID swarm_id) {
    const std::size_t idx = participants_.size();
    participants_.push_back(Participant{
        nullptr,                     // wired below once World exists
        PositionProvider{},          // wired below once World exists
        std::vector<StateVector>{},
        std::vector<Message>{},
    });

    // SimTransport has a private ctor (friend SimBus) — construct via
    // new + adopt into unique_ptr.
    std::unique_ptr<TransportLayer> transport(new SimTransport(*this, idx));

    auto world = std::make_unique<World>(make_world_config(swarm_id),
                                          std::move(transport));

    // Wire the World ref + position provider — both capture the World
    // by raw pointer. World's lifetime exceeds the bus reference
    // (caller owns it).
    World* w_raw = world.get();
    participants_[idx].world = w_raw;
    participants_[idx].pos_fn = [w_raw]() -> PositionComponent {
        return w_raw->registry().get<PositionComponent>(w_raw->self_id());
    };

    // Register the World with the clock so advance() ticks it.
    clock_.register_world(*world);

    return world;
}

void SimBus::advance(std::size_t ticks) {
    clock_.advance(ticks);
}

SimClock&         SimBus::clock()       noexcept { return clock_; }
const SimClock&   SimBus::clock() const noexcept { return clock_; }
std::size_t       SimBus::participant_count() const noexcept { return participants_.size(); }
const SimBusConfig& SimBus::config() const noexcept { return config_; }

void SimBus::deliver_beacon_(std::size_t from_idx, const StateVector& sv) {
    if (from_idx >= participants_.size()) return;
    if (!participants_[from_idx].pos_fn) return;

    const auto from_pos = participants_[from_idx].pos_fn();
    const float range_sq = config_.comm_range_m * config_.comm_range_m;

    for (std::size_t to = 0; to < participants_.size(); ++to) {
        if (to == from_idx) continue;
        if (!participants_[to].pos_fn) continue;

        const auto to_pos = participants_[to].pos_fn();
        const float dx = to_pos.x - from_pos.x;
        const float dy = to_pos.y - from_pos.y;
        const float dz = to_pos.z - from_pos.z;
        const float dist_sq = dx*dx + dy*dy + dz*dz;

        if (dist_sq <= range_sq) {
            participants_[to].inbound_beacons.push_back(sv);
        }
    }

    // Packet loss + latency: RESERVED — not yet enforced.
    (void) config_.packet_loss_pct;
    (void) config_.latency_ms;
}

void SimBus::deliver_message_(std::size_t from_idx, const Message& msg) {
    if (from_idx >= participants_.size()) return;

    // Broadcast (nil recipient unit_id) follows the same range filter
    // as beacons. Directed messages (non-nil recipient) deliver only to
    // the named participant, regardless of range — directed traffic
    // implies known peer, which transport-level routing assumes.
    if (is_broadcast(msg.recipient)) {
        if (!participants_[from_idx].pos_fn) return;
        const auto from_pos = participants_[from_idx].pos_fn();
        const float range_sq = config_.comm_range_m * config_.comm_range_m;

        for (std::size_t to = 0; to < participants_.size(); ++to) {
            if (to == from_idx) continue;
            if (!participants_[to].pos_fn) continue;
            const auto to_pos = participants_[to].pos_fn();
            const float dx = to_pos.x - from_pos.x;
            const float dy = to_pos.y - from_pos.y;
            const float dz = to_pos.z - from_pos.z;
            if (dx*dx + dy*dy + dz*dz <= range_sq) {
                participants_[to].inbound_messages.push_back(msg);
            }
        }
    } else {
        for (std::size_t to = 0; to < participants_.size(); ++to) {
            if (to == from_idx) continue;
            if (!participants_[to].world) continue;
            if (participants_[to].world->identity() == msg.recipient) {
                participants_[to].inbound_messages.push_back(msg);
                break;   // unique recipient
            }
        }
    }
}

void SimBus::drain_inbox_beacons_(std::size_t idx,
                                   std::vector<StateVector>& out) {
    if (idx >= participants_.size()) { out.clear(); return; }
    out = std::move(participants_[idx].inbound_beacons);
    participants_[idx].inbound_beacons.clear();
}

void SimBus::drain_inbox_messages_(std::size_t idx,
                                    std::vector<Message>& out) {
    if (idx >= participants_.size()) { out.clear(); return; }
    out = std::move(participants_[idx].inbound_messages);
    participants_[idx].inbound_messages.clear();
}

} // namespace mith::sim
