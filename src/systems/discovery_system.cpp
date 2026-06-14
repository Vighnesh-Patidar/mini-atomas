#include "mith/systems/discovery_system.h"

#include "mith/comms/message.h"
#include "mith/comms/neighbour_table.h"
#include "mith/comms/transport.h"
#include "mith/core/system.h"
#include "mith/core/world.h"

namespace mith {

DiscoverySystem::DiscoverySystem(World& world) noexcept
    : DiscoverySystem(world, Params{}) {}

DiscoverySystem::DiscoverySystem(World& world, Params params) noexcept
    : world_(&world)
    , neighbour_table_(&world.neighbour_table())
    , message_transport_(world.message_transport())
    , params_(params) {
    // self_id_ is captured at first tick (post-init) — World::identity()
    // aborts pre-init.

    // Register the inbound message handler. BeaconSystem consults this
    // list when draining the message channel; DISCOVERY_HELLO and
    // DISCOVERY_WELCOME are claimed here and never reach mission code's
    // CommBufferComponent.
    world.register_message_handler(
        [this](const Message& m) { return handle_message_(m); });
}

SystemDescriptor DiscoverySystem::describe() const {
    return SystemDescriptor{
        /*name=*/             "DiscoverySystem",
        /*reads_components=*/ {},
        /*writes_components=*/{},
        /*reads_resources=*/  {ResourceID::NeighbourTable},
        /*writes_resources=*/ {ResourceID::TransportTx},   // sends HELLOs / WELCOMEs
    };
}

void DiscoverySystem::tick(EntityRegistry& /*registry*/,
                            const SwarmContext& /*ctx*/,
                            float delta_time) {
    if (!world_) return;

    // Capture self_id_ once World is initialised — identity() aborts
    // pre-init.
    if (self_id_.unit_id.is_nil() && world_->is_initialized()) {
        self_id_ = world_->identity();
    }

    if (state_ == DiscoveryState::Active) return;

    time_in_bootstrap_s_ += delta_time;
    time_since_last_hello_s_ += delta_time;

    // Active HELLO emission during bootstrap (skip if disabled or no
    // transport).
    if (params_.hello_period_s > 0.0f
        && time_since_last_hello_s_ >= params_.hello_period_s
        && message_transport_
        && message_transport_->supports_messages()) {
        Message hello;
        hello.sender     = self_id_;
        hello.recipient  = BROADCAST_ID;
        hello.type       = messages::DISCOVERY_HELLO;
        // seq / timestamp / payload left zero — receivers only need to
        // know the sender's HID to reply.
        message_transport_->send_message(hello);
        ++hellos_sent_;
        time_since_last_hello_s_ = 0.0f;
    }

    // Count distinct peers from the NeighbourTable.
    std::uint32_t count = 0;
    if (neighbour_table_) {
        for (auto it = neighbour_table_->begin();
             it != neighbour_table_->end(); ++it) {
            ++count;
        }
    }
    peers_seen_ = count;

    if (peers_seen_ >= params_.bootstrap_quorum) {
        promote_to_active_();
        return;
    }
    if (time_in_bootstrap_s_ >= params_.bootstrap_timeout_s) {
        promote_to_active_();
    }
}

bool DiscoverySystem::handle_message_(const Message& m) noexcept {
    if (m.type == messages::DISCOVERY_HELLO) {
        ++hellos_received_;
        // Respond with a directed WELCOME to the HELLO's sender. Always
        // respond — peers in Active state still help newcomers bootstrap.
        if (message_transport_ && message_transport_->supports_messages()) {
            Message welcome;
            welcome.sender    = self_id_;
            welcome.recipient = m.sender;
            welcome.type      = messages::DISCOVERY_WELCOME;
            message_transport_->send_message(welcome);
            ++welcomes_sent_;
        }
        return true;
    }
    if (m.type == messages::DISCOVERY_WELCOME) {
        ++welcomes_received_;
        // The accompanying beacon channel (which runs in parallel) will
        // populate the NeighbourTable on its own cycle. The WELCOME's
        // role here is reachability confirmation + observability — no
        // direct NeighbourTable mutation from this slice.
        return true;
    }
    return false;   // not a discovery message — fall through to mission queue
}

void DiscoverySystem::promote_to_active_() noexcept {
    state_ = DiscoveryState::Active;
}

} // namespace mith
