#include "doctest.h"

#include "mith/comms/beacon_system.h"
#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/comms/transport.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"
#include "mith/core/world.h"
#include "mith/identity/hierarchical_id.h"
#include "mith/sim/sim_bus.h"

#include <memory>
#include <vector>

using mith::BeaconSystem;
using mith::CommBufferComponent;
using mith::HierarchicalID;
using mith::Message;
using mith::PositionComponent;
using mith::StateVector;
using mith::SwarmID;
using mith::World;
using mith::WorldConfig;
using mith::sim::SimBus;
using mith::sim::SimBusConfig;

TEST_CASE("BeaconSystem ticks without a transport — ages out, no crash") {
    WorldConfig cfg;
    cfg.tick_rate_hz        = 10.0f;
    cfg.beacon_rate_hz      = 10.0f;
    cfg.neighbour_timeout_s = 0.5f;
    World w(cfg);
    REQUIRE(w.register_system(std::make_unique<BeaconSystem>(w))
            == mith::SchedulerStatus::Ok);
    w.init();

    // Seed a stale neighbour directly in the table.
    StateVector sv;
    sv.id = HierarchicalID::generate(SwarmID{1});
    w.neighbour_table().upsert(sv, /*current_time_s=*/0.0f);
    REQUIRE(w.neighbour_table().count() == 1u);

    // Advance time well past the timeout — BeaconSystem should age it out.
    for (int i = 0; i < 20; ++i) w.tick();   // 2 s at 10 Hz

    CHECK(w.neighbour_table().empty());
    CHECK(w.neighbour_table().total_evictions() == 1u);
}

TEST_CASE("Two Worlds with BeaconSystem learn each other via SimBus") {
    SimBus bus(SimBusConfig{
        /*tick_rate_hz=*/    10.0f,
        /*comm_range_m=*/   100.0f,
        /*loss=*/             0.0f,
        /*latency=*/          0.0f,
    });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});

    REQUIRE(a->register_system(std::make_unique<BeaconSystem>(*a))
            == mith::SchedulerStatus::Ok);
    REQUIRE(b->register_system(std::make_unique<BeaconSystem>(*b))
            == mith::SchedulerStatus::Ok);

    a->init();
    b->init();

    // 10 Hz tick = 10 Hz beacon (default). 20 ticks = 2 s.
    bus.advance(20);

    REQUIRE(a->neighbour_table().count() == 1u);
    REQUIRE(b->neighbour_table().count() == 1u);

    // A's table holds B's identity, and vice versa.
    CHECK(a->neighbour_table().find(b->identity()) != nullptr);
    CHECK(b->neighbour_table().find(a->identity()) != nullptr);
}

TEST_CASE("BeaconSystem skips out-of-range peers (SimBus range filter applies)") {
    SimBus bus(SimBusConfig{
        /*tick_rate_hz=*/  10.0f,
        /*comm_range_m=*/  10.0f,
    });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});

    REQUIRE(a->register_system(std::make_unique<BeaconSystem>(*a))
            == mith::SchedulerStatus::Ok);
    REQUIRE(b->register_system(std::make_unique<BeaconSystem>(*b))
            == mith::SchedulerStatus::Ok);

    a->init();
    b->init();

    // Move B well out of range.
    b->registry().get<PositionComponent>(b->self_id()).x = 50.0f;

    bus.advance(20);

    CHECK(a->neighbour_table().empty());
    CHECK(b->neighbour_table().empty());
}

TEST_CASE("Beacon rate gates send frequency") {
    SimBus bus(SimBusConfig{
        /*tick_rate_hz=*/ 20.0f,   // 50 ms per tick
        /*comm_range_m=*/100.0f,
    });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});

    // B uses default beacon_rate_hz=10 (= one beacon per two ticks).
    REQUIRE(a->register_system(std::make_unique<BeaconSystem>(*a))
            == mith::SchedulerStatus::Ok);
    REQUIRE(b->register_system(std::make_unique<BeaconSystem>(*b))
            == mith::SchedulerStatus::Ok);

    a->init();
    b->init();

    // 10 ticks at 20 Hz = 500 ms. At 10 Hz beacon, we expect ~5 beacons
    // each way. NeighbourTable accumulates observations counter on every
    // upsert; check it landed somewhere reasonable.
    bus.advance(10);

    CHECK(a->neighbour_table().count() == 1u);
    CHECK(a->neighbour_table().total_observations() >= 1u);
    CHECK(a->neighbour_table().total_observations() <= 10u);
}

TEST_CASE("BeaconSystem populates StateVector with current self-component values") {
    SimBus bus(SimBusConfig{ 10.0f, 100.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});

    REQUIRE(a->register_system(std::make_unique<BeaconSystem>(*a))
            == mith::SchedulerStatus::Ok);
    REQUIRE(b->register_system(std::make_unique<BeaconSystem>(*b))
            == mith::SchedulerStatus::Ok);

    a->init();
    b->init();

    // Set A's components to recognisable values before any ticks.
    a->registry().get<PositionComponent>(a->self_id()) = PositionComponent{1.0f, 2.0f, 3.0f};
    a->registry().get<mith::VelocityComponent>(a->self_id()) = mith::VelocityComponent{4.0f, 5.0f, 6.0f};
    a->registry().get<mith::HealthComponent>(a->self_id()).value = 77u;
    a->registry().get<mith::RoleComponent>(a->self_id()).role    = 9u;
    a->registry().get<mith::BehaviourStateComponent>(a->self_id()).state = 11u;

    bus.advance(5);

    const auto* a_entry = b->neighbour_table().find(a->identity());
    REQUIRE(a_entry != nullptr);
    CHECK(a_entry->position.x == 1.0f);
    CHECK(a_entry->position.z == 3.0f);
    CHECK(a_entry->velocity.vx == 4.0f);
    CHECK(a_entry->health.value == 77u);
    CHECK(a_entry->role.role == 9u);
    CHECK(a_entry->state.state == 11u);
}

TEST_CASE("BeaconSystem pushes received Messages into CommBufferComponent") {
    SimBus bus(SimBusConfig{ 10.0f, 100.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});

    REQUIRE(a->register_system(std::make_unique<BeaconSystem>(*a))
            == mith::SchedulerStatus::Ok);
    REQUIRE(b->register_system(std::make_unique<BeaconSystem>(*b))
            == mith::SchedulerStatus::Ok);

    a->init();
    b->init();

    Message m;
    m.sender    = a->identity();
    m.recipient = b->identity();        // directed at B
    m.type      = mith::messages::TASK_BID;
    m.seq       = 99;
    REQUIRE(a->transport()->send_message(m));

    // B ticks → polls → pushes the Message into its CommBufferComponent.
    bus.advance(1);

    auto& cb = b->registry().get<CommBufferComponent>(b->self_id());
    REQUIRE(cb.queue.size() == 1u);
    auto first = cb.queue.pop();
    REQUIRE(first.has_value());
    CHECK(first->seq == 99u);
    CHECK(first->type == mith::messages::TASK_BID);
}

TEST_CASE("Aged-out neighbours are re-acquired if they come back in range") {
    SimBus bus(SimBusConfig{ 10.0f, /*range=*/10.0f });
    auto a = bus.create_world(SwarmID{1});
    auto b = bus.create_world(SwarmID{1});

    WorldConfig acfg_view = a->config();
    REQUIRE(acfg_view.neighbour_timeout_s == doctest::Approx(0.5f));   // default

    REQUIRE(a->register_system(std::make_unique<BeaconSystem>(*a))
            == mith::SchedulerStatus::Ok);
    REQUIRE(b->register_system(std::make_unique<BeaconSystem>(*b))
            == mith::SchedulerStatus::Ok);

    a->init();
    b->init();

    bus.advance(5);
    REQUIRE(a->neighbour_table().count() == 1u);

    // Move B out of range.
    b->registry().get<PositionComponent>(b->self_id()).x = 50.0f;

    // Tick long enough for A's entry of B to age out (timeout 0.5 s, tick
    // 0.1 s — 6+ ticks of silence should do it).
    bus.advance(10);
    CHECK(a->neighbour_table().empty());

    // Move B back in range and tick again.
    b->registry().get<PositionComponent>(b->self_id()).x = 0.0f;
    bus.advance(5);

    CHECK(a->neighbour_table().count() == 1u);
}
