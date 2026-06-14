#pragma once

// DiscoverySystem — see ARCHITECTURE.md §16 v0.3
//
// Bootstrap state machine + active discovery for new robots joining a
// swarm without a mission controller. Each robot starts in
// DiscoveryState::Bootstrapping, observes the NeighbourTable as peers'
// beacons arrive, and promotes itself to DiscoveryState::Active when any
// of:
//
//   - It has seen at least `bootstrap_quorum` distinct peers, OR
//   - It has spent at least `bootstrap_timeout_s` seconds in bootstrap.
//
// Active discovery (this slice): during Bootstrapping, the system
// broadcasts DISCOVERY_HELLO messages every `hello_period_s` to solicit
// immediate replies. Peers (regardless of their own discovery state)
// respond with a unicast DISCOVERY_WELCOME containing their identity —
// the requester's beacon channel will populate the NeighbourTable on
// the next beacon period anyway, but the WELCOME confirms reachability
// faster on slow-beacon links (e.g. radio with multi-second beacons).
//
// DISCOVERY_HELLO / DISCOVERY_WELCOME never reach mission code's
// CommBufferComponent — DiscoverySystem registers a message handler
// with World that claims them. Mission code sees a clean queue.
//
// Signed-mode pubkey registration is deferred — depends on a later
// v0.3 slice extending StateVector / WELCOME with public-key carriage.
//
// Deterministic: same observation + delivery schedule → same transition
// tick. Pairs cleanly with SimBus's Sequential default.

#include "mith/api_stability.h"
#include "mith/comms/message.h"
#include "mith/core/system.h"
#include "mith/identity/hierarchical_id.h"

#include <cstdint>

namespace mith {

class World;
class NeighbourTable;
class MessageTransport;

enum class DiscoveryState : std::uint8_t {
    Bootstrapping = 0,
    Active        = 1,
};

class MITH_STABLE_API DiscoverySystem : public System {
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

        // How often to broadcast DISCOVERY_HELLO during Bootstrapping.
        // 0 disables active discovery — system runs purely on passive
        // NeighbourTable observation. Once Active, HELLOs stop firing
        // (we no longer need accelerated peer discovery).
        float hello_period_s = 1.0f;
    };

    explicit DiscoverySystem(World& world) noexcept;
    DiscoverySystem(World& world, Params params) noexcept;

    SystemDescriptor describe() const override;
    void tick(EntityRegistry& registry,
              const SwarmContext& ctx,
              float delta_time) override;

    // Observability accessors — readable by tests and mission code.
    DiscoveryState current_state() const noexcept       { return state_; }
    bool           is_bootstrapping() const noexcept    { return state_ == DiscoveryState::Bootstrapping; }
    bool           is_active() const noexcept           { return state_ == DiscoveryState::Active; }
    std::uint32_t  peers_seen() const noexcept          { return peers_seen_; }
    float          time_in_bootstrap_s() const noexcept { return time_in_bootstrap_s_; }
    std::uint64_t  hellos_sent() const noexcept         { return hellos_sent_; }
    std::uint64_t  welcomes_sent() const noexcept       { return welcomes_sent_; }
    std::uint64_t  welcomes_received() const noexcept   { return welcomes_received_; }
    std::uint64_t  hellos_received() const noexcept     { return hellos_received_; }

private:
    void promote_to_active_() noexcept;
    bool handle_message_(const Message& m) noexcept;

    World*            world_              = nullptr;
    NeighbourTable*   neighbour_table_    = nullptr;
    MessageTransport* message_transport_  = nullptr;
    HierarchicalID    self_id_{};

    Params          params_;
    DiscoveryState  state_                = DiscoveryState::Bootstrapping;
    std::uint32_t   peers_seen_           = 0;
    float           time_in_bootstrap_s_  = 0.0f;
    float           time_since_last_hello_s_ = 0.0f;

    std::uint64_t   hellos_sent_          = 0;
    std::uint64_t   welcomes_sent_        = 0;
    std::uint64_t   welcomes_received_    = 0;
    std::uint64_t   hellos_received_      = 0;
};

} // namespace mith
