#include "doctest.h"

#include "mith/comms/message.h"
#include "mith/comms/state_vector.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"
#include "mith/core/world.h"
#include "mith/identity/hierarchical_id.h"
#include "mith/sim/sim_bus.h"
#include "mith/sim/sim_clock.h"

#include <memory>
#include <vector>

using mith::HierarchicalID;
using mith::Message;
using mith::PositionComponent;
using mith::StateVector;
using mith::SwarmID;
using mith::TransportLayer;
using mith::World;
using mith::WorldConfig;
using mith::sim::SimBus;
using mith::sim::SimBusConfig;
using mith::sim::SimClock;

// ------------------------------------------------------------------------
// SimClock
// ------------------------------------------------------------------------

TEST_CASE("default-constructed SimClock: 50 ms delta, zero counters") {
    SimClock c;
    CHECK(c.delta_time_s() == doctest::Approx(0.05f));
    CHECK(c.elapsed_time_s() == 0.0f);
    CHECK(c.tick_count() == 0u);
    CHECK(c.world_count() == 0u);
}

TEST_CASE("SimClock::advance ticks every registered World N times") {
    WorldConfig cfg;
    cfg.tick_rate_hz = 20.0f;
    World a(cfg), b(cfg);
    a.init();
    b.init();

    SimClock c(0.05f);
    c.register_world(a);
    c.register_world(b);
    CHECK(c.world_count() == 2u);

    c.advance(5);

    CHECK(c.tick_count() == 5u);
    CHECK(c.elapsed_time_s() == doctest::Approx(0.25f));
    CHECK(a.context().tick_count == 5u);
    CHECK(b.context().tick_count == 5u);
}

TEST_CASE("SimClock::register_world is idempotent (no duplicate ticks)") {
    World w(WorldConfig{});
    w.init();
    SimClock c;
    c.register_world(w);
    c.register_world(w);   // duplicate registration ignored
    CHECK(c.world_count() == 1u);

    c.advance(3);
    CHECK(w.context().tick_count == 3u);   // not 6
}

TEST_CASE("SimClock::unregister_all clears world list but preserves counters") {
    World w(WorldConfig{});
    w.init();
    SimClock c;
    c.register_world(w);
    c.advance(2);
    REQUIRE(c.tick_count() == 2u);

    c.unregister_all();
    CHECK(c.world_count() == 0u);
    CHECK(c.tick_count() == 2u);           // monotonic, preserved
    CHECK(c.elapsed_time_s() == doctest::Approx(0.10f));
}

// ------------------------------------------------------------------------
// SimBus + SimTransport
// ------------------------------------------------------------------------

TEST_CASE("SimBus::make_world_config carries Sequential + tick_rate") {
    SimBus bus(SimBusConfig{ /*tick_rate_hz=*/50.0f });
    auto cfg = bus.make_world_config(SwarmID{1});

    CHECK(cfg.swarm_id == 1u);
    CHECK(cfg.tick_rate_hz == 50.0f);
    CHECK(cfg.scheduler_mode == mith::SchedulerMode::Sequential);
}

TEST_CASE("SimBus::create_world wires a SimTransport into the World") {
    SimBus bus;
    auto w = bus.create_world(SwarmID{1});
    REQUIRE(w != nullptr);

    w->init();

    // The World now has a transport — SimBus made it.
    CHECK(w->transport() != nullptr);
    CHECK(w->transport()->is_healthy());

    // And the clock has one participant.
    CHECK(bus.clock().world_count() == 1u);
    CHECK(bus.participant_count() == 1u);
}

TEST_CASE("SimBus::advance ticks every created World") {
    SimBus bus(SimBusConfig{ /*tick_rate_hz=*/20.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});
    a->init();
    b->init();

    bus.advance(10);

    CHECK(a->context().tick_count == 10u);
    CHECK(b->context().tick_count == 10u);
    CHECK(bus.clock().tick_count() == 10u);
}

TEST_CASE("beacon sent by one World is delivered to the other (in range)") {
    SimBus bus(SimBusConfig{ /*tick=*/20.0f, /*range=*/100.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});
    a->init();
    b->init();

    // Both Worlds are at origin (default PositionComponent) — within range.
    StateVector sv;
    sv.id = a->identity();
    sv.position = a->registry().get<PositionComponent>(a->self_id());

    REQUIRE(a->transport()->send_beacon(sv));

    std::vector<StateVector> beacons;
    std::vector<Message>     messages;
    b->transport()->poll(beacons, messages);

    REQUIRE(beacons.size() == 1u);
    CHECK(beacons[0].id == a->identity());
    CHECK(messages.empty());
}

TEST_CASE("beacon is NOT delivered to a World out of range") {
    SimBus bus(SimBusConfig{ /*tick=*/20.0f, /*range=*/10.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});
    a->init();
    b->init();

    // Place B 50m away from A on the x axis (range is 10m).
    b->registry().get<PositionComponent>(b->self_id()).x = 50.0f;

    StateVector sv;
    sv.id = a->identity();
    REQUIRE(a->transport()->send_beacon(sv));

    std::vector<StateVector> beacons;
    std::vector<Message>     messages;
    b->transport()->poll(beacons, messages);

    CHECK(beacons.empty());      // out of range
    CHECK(messages.empty());
}

TEST_CASE("sender does NOT receive its own beacon") {
    SimBus bus;
    auto a = bus.create_world(SwarmID{1});
    a->init();

    StateVector sv;
    sv.id = a->identity();
    REQUIRE(a->transport()->send_beacon(sv));

    std::vector<StateVector> beacons;
    std::vector<Message>     messages;
    a->transport()->poll(beacons, messages);

    CHECK(beacons.empty());      // no self-loop
}

TEST_CASE("broadcast Message follows the range filter") {
    SimBus bus(SimBusConfig{ /*tick=*/20.0f, /*range=*/5.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});
    auto c = bus.create_world(SwarmID{1});
    a->init(); b->init(); c->init();

    // B at 3m (in range), C at 10m (out of range).
    b->registry().get<PositionComponent>(b->self_id()).x = 3.0f;
    c->registry().get<PositionComponent>(c->self_id()).x = 10.0f;

    Message m;
    m.sender    = a->identity();
    m.recipient = mith::BROADCAST_ID;
    m.seq       = 42;
    REQUIRE(a->transport()->send_message(m));

    std::vector<StateVector> beacons;
    std::vector<Message>     messages;
    b->transport()->poll(beacons, messages);
    CHECK(messages.size() == 1u);
    if (!messages.empty()) CHECK(messages[0].seq == 42u);

    messages.clear();
    c->transport()->poll(beacons, messages);
    CHECK(messages.empty());      // C is out of range
}

TEST_CASE("directed Message delivers only to the named recipient") {
    SimBus bus(SimBusConfig{ /*tick=*/20.0f, /*range=*/5.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});
    auto c = bus.create_world(SwarmID{1});
    a->init(); b->init(); c->init();

    // C is far away — directed delivery ignores range (per impl).
    c->registry().get<PositionComponent>(c->self_id()).x = 1000.0f;

    Message m;
    m.sender    = a->identity();
    m.recipient = c->identity();    // directed at C
    m.seq       = 99;
    REQUIRE(a->transport()->send_message(m));

    std::vector<StateVector> beacons;
    std::vector<Message>     messages;

    // B (not addressed) gets nothing.
    b->transport()->poll(beacons, messages);
    CHECK(messages.empty());

    // C (addressed) gets it despite range.
    c->transport()->poll(beacons, messages);
    REQUIRE(messages.size() == 1u);
    CHECK(messages[0].seq == 99u);
    CHECK(messages[0].recipient == c->identity());
}

TEST_CASE("poll() drains the inbox — subsequent polls return empty") {
    SimBus bus;
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});
    a->init(); b->init();

    StateVector sv;
    sv.id = a->identity();
    REQUIRE(a->transport()->send_beacon(sv));
    REQUIRE(a->transport()->send_beacon(sv));

    std::vector<StateVector> beacons;
    std::vector<Message>     messages;

    b->transport()->poll(beacons, messages);
    CHECK(beacons.size() == 2u);

    b->transport()->poll(beacons, messages);
    CHECK(beacons.empty());      // inbox drained
}

TEST_CASE("a 3-robot sim ticks deterministically; SimClock advances counters") {
    SimBus bus(SimBusConfig{ /*tick=*/100.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});
    auto c = bus.create_world(SwarmID{1});
    a->init(); b->init(); c->init();

    bus.advance(100);

    CHECK(a->context().tick_count == 100u);
    CHECK(b->context().tick_count == 100u);
    CHECK(c->context().tick_count == 100u);
    CHECK(bus.clock().tick_count() == 100u);
    CHECK(bus.clock().elapsed_time_s() == doctest::Approx(1.0f));
}
