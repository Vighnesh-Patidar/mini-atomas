#include "doctest.h"

// End-to-end test for SerialTransport using a socketpair to stand in
// for a UART. Verifies the full path: encode beacon/message → write to
// fd → read fd → decode framed payload → udp_wire decode → inbox.
//
// Gated on MITH_SERIAL_ENABLED. No real serial device required.

#ifdef MITH_SERIAL_ENABLED

#include "mith/comms/serial_transport.h"
#include "mith/comms/state_vector.h"
#include "mith/identity/hierarchical_id.h"

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <vector>

using mith::HierarchicalID;
using mith::Message;
using mith::SerialTransport;
using mith::StateVector;
using mith::SwarmID;

namespace {

template <typename Pred>
bool wait_for(Pred pred, int max_ms = 200) {
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(max_ms);
    while (steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(milliseconds(5));
    }
    return pred();
}

struct PairedTransports {
    std::unique_ptr<SerialTransport> a;
    std::unique_ptr<SerialTransport> b;
};

PairedTransports make_pair() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        return {};   // fail — tests below check for empty
    }
    return {
        SerialTransport::for_fd(fds[0], /*own_fd=*/true),
        SerialTransport::for_fd(fds[1], /*own_fd=*/true),
    };
}

} // namespace

TEST_CASE("Serial integration: beacon round-trip through socketpair") {
    auto p = make_pair();
    REQUIRE(p.a);
    REQUIRE(p.b);

    StateVector sv;
    sv.id          = HierarchicalID::generate(SwarmID{1});
    sv.position.x  = 1.5f;
    sv.position.y  = 2.5f;
    sv.position.z  = 3.5f;
    sv.tick        = 999u;

    REQUIRE(p.a->send_beacon(sv));

    std::vector<StateVector> received;
    const bool ok = wait_for([&]{
        received.clear();
        p.b->poll_beacons(received);
        return !received.empty();
    });
    REQUIRE(ok);
    REQUIRE(received.size() == 1u);
    CHECK(received[0].id == sv.id);
    CHECK(received[0].position.x == 1.5f);
    CHECK(received[0].tick == 999u);
}

TEST_CASE("Serial integration: message round-trip + per-channel routing") {
    auto p = make_pair();
    REQUIRE(p.a);
    REQUIRE(p.b);

    Message m;
    m.sender    = HierarchicalID::generate(SwarmID{2});
    m.recipient = HierarchicalID::generate(SwarmID{2});
    m.type      = 0x77;
    m.seq       = 42u;
    m.payload[0] = 0xDE;
    m.payload[1] = 0xAD;

    REQUIRE(p.a->send_message(m));

    // Poll beacons first — should be empty since we sent only a message.
    std::vector<StateVector> beacons;
    p.b->poll_beacons(beacons);
    CHECK(beacons.empty());

    std::vector<Message> received;
    const bool ok = wait_for([&]{
        received.clear();
        p.b->poll_messages(received);
        return !received.empty();
    });
    REQUIRE(ok);
    REQUIRE(received.size() == 1u);
    CHECK(received[0].sender == m.sender);
    CHECK(received[0].seq == 42u);
    CHECK(received[0].payload[0] == 0xDE);
    CHECK(received[0].payload[1] == 0xAD);
}

TEST_CASE("Serial integration: multiple back-to-back beacons all arrive") {
    auto p = make_pair();
    REQUIRE(p.a);
    REQUIRE(p.b);

    for (int i = 0; i < 5; ++i) {
        StateVector sv;
        sv.id   = HierarchicalID::generate(SwarmID{3});
        sv.tick = static_cast<std::uint32_t>(i + 1);
        REQUIRE(p.a->send_beacon(sv));
    }

    std::vector<StateVector> received;
    const bool ok = wait_for([&]{
        std::vector<StateVector> chunk;
        p.b->poll_beacons(chunk);
        for (auto& sv : chunk) received.push_back(sv);
        return received.size() >= 5u;
    });
    REQUIRE(ok);
    CHECK(received.size() >= 5u);
    CHECK(p.b->frames_decoded() >= 5u);
}

#endif // MITH_SERIAL_ENABLED
