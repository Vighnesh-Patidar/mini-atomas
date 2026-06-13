#include "doctest.h"

#include "mith/comms/message.h"
#include "mith/comms/state_vector.h"
#include "mith/comms/transport.h"
#include "mith/identity/hierarchical_id.h"

#include <cstdint>
#include <vector>

using mith::HierarchicalID;
using mith::Message;
using mith::StateVector;
using mith::SwarmID;
using mith::TransportLayer;

namespace {

// A null transport — drops everything on send, returns empty on poll.
// Used to exercise the TransportLayer interface contract without an
// actual transport impl.
class NullTransport : public TransportLayer {
public:
    bool send_beacon(const StateVector&) override { return true; }
    bool send_message(const Message&) override    { return true; }
    void poll(std::vector<StateVector>& beacons_out,
              std::vector<Message>&     messages_out) override {
        beacons_out.clear();
        messages_out.clear();
    }
    bool is_healthy() const override { return true; }
};

// A counting transport — captures sent traffic and reports counts.
class CountingTransport : public TransportLayer {
public:
    std::size_t beacons_sent  = 0;
    std::size_t messages_sent = 0;
    bool healthy              = true;

    bool send_beacon(const StateVector&) override { ++beacons_sent;  return true; }
    bool send_message(const Message&) override    { ++messages_sent; return true; }
    void poll(std::vector<StateVector>& beacons_out,
              std::vector<Message>&     messages_out) override {
        beacons_out.clear();
        messages_out.clear();
    }
    bool is_healthy() const override { return healthy; }
};

} // namespace

TEST_CASE("TransportLayer is usable as an abstract base") {
    NullTransport null;
    TransportLayer* poly = &null;

    StateVector sv;
    sv.id = HierarchicalID::generate(SwarmID{1});

    CHECK(poly->send_beacon(sv));
    CHECK(poly->send_message(Message{}));
    CHECK(poly->is_healthy());

    std::vector<StateVector> beacons{StateVector{}};
    std::vector<Message>     messages{Message{}};
    poly->poll(beacons, messages);
    CHECK(beacons.empty());     // poll clears the output vectors
    CHECK(messages.empty());
}

TEST_CASE("send_beacon and send_message dispatch through the virtual interface") {
    CountingTransport t;
    TransportLayer* poly = &t;

    poly->send_beacon(StateVector{});
    poly->send_beacon(StateVector{});
    poly->send_message(Message{});

    CHECK(t.beacons_sent == 2u);
    CHECK(t.messages_sent == 1u);
}

TEST_CASE("is_healthy() reflects implementation state") {
    CountingTransport t;
    CHECK(t.is_healthy());
    t.healthy = false;
    CHECK_FALSE(t.is_healthy());
}
