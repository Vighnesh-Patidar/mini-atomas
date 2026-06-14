#pragma once

// DiscoverySystem — see ARCHITECTURE.md §16 v0.3 (first slice)
//
// Bootstrap state machine for new robots joining a swarm without a
// mission controller. Each robot starts in DiscoveryState::Bootstrapping,
// observes the NeighbourTable as peers' beacons arrive, and promotes
// itself to DiscoveryState::Active once any of:
//
//   - It has seen at least `bootstrap_quorum` distinct peers, OR
//   - It has spent at least `bootstrap_timeout_s` seconds in bootstrap
//     (graceful fallback — single-robot deployments don't deadlock).
//
// Mission code is expected to read current_state() / is_bootstrapping()
// and gate its own behaviour (e.g. FlockingSystem stays passive, task
// allocation defers) until the robot transitions to Active. The runtime
// does NOT enforce this gating — that's policy, not mechanism.
//
// Active HELLO / WELCOME message exchange and signed-mode pubkey
// registration are planned for the next v0.3 slice; this first slice
// is purely the passive observation + state machine.
//
// Deterministic: same NeighbourTable population schedule → same
// transition tick. Pairs cleanly with SimBus's Sequential default.

#include "mith/core/system.h"

#include <cstdint>

namespace mith {

class World;
class NeighbourTable;

enum class DiscoveryState : std::uint8_t {
    Bootstrapping = 0,
    Active        = 1,
};

class DiscoverySystem : public System {
public:
    struct Params {
        // Minimum number of distinct peers observed in the NeighbourTable
        // before promoting from Bootstrapping → Active. 0 means "promote
        // immediately on first tick" (testing / single-node deployments).
        std::uint32_t bootstrap_quorum = 1;

        // Hard timeout. After this many seconds in Bootstrapping, promote
        // regardless of peer count. Prevents a permanent deadlock when
        // the robot is alone or the link is down.
        float bootstrap_timeout_s = 30.0f;
    };

    explicit DiscoverySystem(World& world) noexcept;
    DiscoverySystem(World& world, Params params) noexcept;

    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

    // Observability accessors — readable by tests and mission code.
    DiscoveryState current_state() const noexcept    { return state_; }
    bool           is_bootstrapping() const noexcept { return state_ == DiscoveryState::Bootstrapping; }
    bool           is_active() const noexcept        { return state_ == DiscoveryState::Active; }
    std::uint32_t  peers_seen() const noexcept       { return peers_seen_; }
    float          time_in_bootstrap_s() const noexcept { return time_in_bootstrap_s_; }

private:
    void promote_to_active_() noexcept;

    NeighbourTable* neighbour_table_;
    Params          params_;
    DiscoveryState  state_                  = DiscoveryState::Bootstrapping;
    std::uint32_t   peers_seen_             = 0;
    float           time_in_bootstrap_s_    = 0.0f;
};

} // namespace mith
