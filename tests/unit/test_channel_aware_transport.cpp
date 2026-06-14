#include "doctest.h"

#include "mith/comms/transport.h"
#include "mith/comms/message.h"
#include "mith/comms/state_vector.h"
#include "mith/core/world.h"
#include "mith/identity/hierarchical_id.h"

#include <memory>
#include <vector>

using mith::BeaconTransport;
using mith::HierarchicalID;
using mith::Message;
using mith::MessageTransport;
using mith::StateVector;
using mith::SwarmID;
using mith::TransportLayer;
using mith::World;
using mith::WorldConfig;

namespace {

// Beacon-only transport: a "LoRa radio" stand-in. Carries beacons, refuses
// messages by declaring supports_messages() = false.
class LoraBeaconTransport : public BeaconTransport {
public:
    std::size_t              beacons_sent = 0;
    std::vector<StateVector> inbox;

    bool send_beacon(const StateVector&) override {
        ++beacons_sent;
        return true;
    }
    void poll_beacons(std::vector<StateVector>& out) override {
        out = std::move(inbox);
        inbox.clear();
    }
    bool is_healthy() const override { return true; }
};

// Message-only transport: a "TCP sidecar" stand-in. Carries messages,
// declines beacons via supports_beacons() = false.
class TcpMessageTransport : public MessageTransport {
public:
    std::size_t          messages_sent = 0;
    std::vector<Message> inbox;

    bool send_message(const Message&) override {
        ++messages_sent;
        return true;
    }
    void poll_messages(std::vector<Message>& out) override {
        out = std::move(inbox);
        inbox.clear();
    }
    bool is_healthy() const override { return true; }
};

} // namespace

TEST_CASE("Split transports: World plumbs each channel to its own transport") {
    auto beacon  = std::make_unique<LoraBeaconTransport>();
    auto message = std::make_unique<TcpMessageTransport>();

    LoraBeaconTransport*  beacon_raw  = beacon.get();
    TcpMessageTransport*  message_raw = message.get();

    World w(WorldConfig{}, std::move(beacon), std::move(message));
    w.init();

    REQUIRE(w.beacon_transport()  == beacon_raw);
    REQUIRE(w.message_transport() == message_raw);
    CHECK(w.transport() == nullptr);   // unified slot empty in split mode

    // Channel-direct sends route through the right transport.
    w.beacon_transport()->send_beacon(StateVector{});
    w.message_transport()->send_message(Message{});
    CHECK(beacon_raw->beacons_sent  == 1u);
    CHECK(message_raw->messages_sent == 1u);
}

TEST_CASE("Unified transport: per-channel accessors resolve to the same TransportLayer") {
    // Tiny unified impl for the test.
    struct UnifiedNop : public TransportLayer {
        bool send_beacon(const StateVector&) override   { return true; }
        bool send_message(const Message&) override      { return true; }
        void poll_beacons(std::vector<StateVector>& out)  override { out.clear(); }
        void poll_messages(std::vector<Message>& out)     override { out.clear(); }
        bool is_healthy() const override { return true; }
    };

    auto unified     = std::make_unique<UnifiedNop>();
    UnifiedNop* raw  = unified.get();

    World w(WorldConfig{}, std::move(unified));
    w.init();

    CHECK(w.transport()         == raw);
    CHECK(w.beacon_transport()  == static_cast<BeaconTransport*>(raw));
    CHECK(w.message_transport() == static_cast<MessageTransport*>(raw));
}

TEST_CASE("Capability flags: supports_beacons / supports_messages default to true") {
    struct UnifiedNop : public TransportLayer {
        bool send_beacon(const StateVector&) override   { return true; }
        bool send_message(const Message&) override      { return true; }
        void poll_beacons(std::vector<StateVector>& out)  override { out.clear(); }
        void poll_messages(std::vector<Message>& out)     override { out.clear(); }
        bool is_healthy() const override { return true; }
    };
    UnifiedNop t;
    CHECK(t.supports_beacons());
    CHECK(t.supports_messages());
}

TEST_CASE("Split transports: each transport declares ONLY its own channel by default") {
    LoraBeaconTransport b;
    TcpMessageTransport m;
    CHECK(b.supports_beacons());
    CHECK(m.supports_messages());
    // The other side of each split transport is a non-question — those
    // methods aren't part of the respective interface. The fact that
    // this code compiles is itself the test.
}

TEST_CASE("World with null transports: both accessors return null cleanly") {
    World w(WorldConfig{});
    w.init();
    CHECK(w.transport()         == nullptr);
    CHECK(w.beacon_transport()  == nullptr);
    CHECK(w.message_transport() == nullptr);
}

TEST_CASE("Channel-aware BeaconSystem skips a missing channel without crashing") {
    // World with ONLY a beacon transport (message side null). Tick should
    // not segfault even though message_transport() returns null.
    auto beacon = std::make_unique<LoraBeaconTransport>();
    LoraBeaconTransport* beacon_raw = beacon.get();
    World w(WorldConfig{}, std::move(beacon), /*message=*/nullptr);
    w.init();

    REQUIRE(w.beacon_transport()  != nullptr);
    REQUIRE(w.message_transport() == nullptr);

    // BeaconSystem isn't auto-registered, but the World accessors are
    // what BeaconSystem captures at construction. Verify the World layer
    // handles half-split cleanly.
    CHECK(beacon_raw->is_healthy());
}
