#include "mith/sim/sim_bus.h"

#include "mith/core/builtin_components.h"

#include <cmath>
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
    for (std::size_t i = 0; i < ticks; ++i) {
        rebuild_grid_();    // positions are stale-by-one-tick within the step
        clock_.advance(1);
    }
}

SimBus::CellKey SimBus::cell_for_(float x, float y, float z) const noexcept {
    if (grid_cell_size_m_ <= 0.0f) return CellKey{0, 0, 0};
    const float inv = 1.0f / grid_cell_size_m_;
    return CellKey{
        static_cast<std::int32_t>(std::floor(x * inv)),
        static_cast<std::int32_t>(std::floor(y * inv)),
        static_cast<std::int32_t>(std::floor(z * inv)),
    };
}

void SimBus::rebuild_grid_() {
    // Cell size defaults to comm_range_m — one-cell-radius scan covers
    // every possible recipient.
    grid_cell_size_m_ = config_.comm_range_m;
    grid_.clear();
    if (grid_cell_size_m_ <= 0.0f) return;
    for (std::size_t i = 0; i < participants_.size(); ++i) {
        if (!participants_[i].pos_fn) continue;
        const auto p = participants_[i].pos_fn();
        grid_[cell_for_(p.x, p.y, p.z)].push_back(i);
    }
}

SimClock&         SimBus::clock()       noexcept { return clock_; }
const SimClock&   SimBus::clock() const noexcept { return clock_; }
std::size_t       SimBus::participant_count() const noexcept { return participants_.size(); }
const SimBusConfig& SimBus::config() const noexcept { return config_; }

void SimBus::deliver_beacon_(std::size_t from_idx, const StateVector& sv) {
    if (from_idx >= participants_.size()) return;
    if (!participants_[from_idx].pos_fn) return;

    const auto  from_pos = participants_[from_idx].pos_fn();
    const float range_sq = config_.comm_range_m * config_.comm_range_m;

    // Spatial-index path. Grid is populated by rebuild_grid_() at the
    // top of each advance() tick. Fallback (linear scan) handles the
    // "0 ticks elapsed yet" case + the disabled (cell_size_m == 0) case.
    if (grid_cell_size_m_ > 0.0f && !grid_.empty()) {
        const auto centre = cell_for_(from_pos.x, from_pos.y, from_pos.z);
        // span = 1: comm_range_m is exactly cell_size, so any recipient
        // lives in centre OR one of the 26 adjacent cells.
        const int span = 1;
        for (int dz = -span; dz <= span; ++dz) {
            for (int dy = -span; dy <= span; ++dy) {
                for (int dx = -span; dx <= span; ++dx) {
                    CellKey key{centre[0] + dx, centre[1] + dy, centre[2] + dz};
                    auto it = grid_.find(key);
                    if (it == grid_.end()) continue;
                    for (std::size_t to : it->second) {
                        if (to == from_idx) continue;
                        if (!participants_[to].pos_fn) continue;
                        const auto to_pos = participants_[to].pos_fn();
                        const float ex = to_pos.x - from_pos.x;
                        const float ey = to_pos.y - from_pos.y;
                        const float ez = to_pos.z - from_pos.z;
                        if (ex*ex + ey*ey + ez*ez <= range_sq) {
                            participants_[to].inbound_beacons.push_back(sv);
                        }
                    }
                }
            }
        }
    } else {
        for (std::size_t to = 0; to < participants_.size(); ++to) {
            if (to == from_idx) continue;
            if (!participants_[to].pos_fn) continue;

            const auto to_pos = participants_[to].pos_fn();
            const float dx = to_pos.x - from_pos.x;
            const float dy = to_pos.y - from_pos.y;
            const float dz = to_pos.z - from_pos.z;
            if (dx*dx + dy*dy + dz*dz <= range_sq) {
                participants_[to].inbound_beacons.push_back(sv);
            }
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

        if (grid_cell_size_m_ > 0.0f && !grid_.empty()) {
            const auto centre = cell_for_(from_pos.x, from_pos.y, from_pos.z);
            const int span = 1;
            for (int dz = -span; dz <= span; ++dz) {
                for (int dy = -span; dy <= span; ++dy) {
                    for (int dx = -span; dx <= span; ++dx) {
                        CellKey key{centre[0] + dx, centre[1] + dy, centre[2] + dz};
                        auto it = grid_.find(key);
                        if (it == grid_.end()) continue;
                        for (std::size_t to : it->second) {
                            if (to == from_idx) continue;
                            if (!participants_[to].pos_fn) continue;
                            const auto to_pos = participants_[to].pos_fn();
                            const float ex = to_pos.x - from_pos.x;
                            const float ey = to_pos.y - from_pos.y;
                            const float ez = to_pos.z - from_pos.z;
                            if (ex*ex + ey*ey + ez*ez <= range_sq) {
                                participants_[to].inbound_messages.push_back(msg);
                            }
                        }
                    }
                }
            }
        } else {
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
