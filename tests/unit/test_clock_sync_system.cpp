#include "doctest.h"

#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/core/world.h"
#include "mith/identity/hierarchical_id.h"
#include "mith/systems/clock_sync_system.h"

using mith::ClockSyncSystem;
using mith::HierarchicalID;
using mith::NeighbourTable;
using mith::StateVector;
using mith::SwarmID;
using mith::World;
using mith::WorldConfig;

namespace {

void seed_peer(NeighbourTable& nt, float sync_time_s) {
    StateVector sv;
    sv.id          = HierarchicalID::generate(SwarmID{1});
    sv.sync_time_s = sync_time_s;
    nt.upsert(sv, 0.0f);
}

} // namespace

TEST_CASE("ClockSyncSystem: zero peers → offset stays at 0 and no adjustments fire") {
    World w(WorldConfig{});
    w.init();
    ClockSyncSystem cs(w);

    for (int i = 0; i < 20; ++i) cs.tick(w.registry(), w.context(), 0.1f);

    CHECK(w.clock_offset_s() == 0.0f);
    CHECK(cs.adjustments_applied() == 0u);
    CHECK(cs.observations() == 0u);
}

TEST_CASE("ClockSyncSystem: one peer ahead of us pulls our offset positive") {
    World w(WorldConfig{});
    w.init();

    // Peer reports sync_time = 5.0 s; we're at 0.0 → gap = +5.0.
    seed_peer(w.neighbour_table(), 5.0f);

    ClockSyncSystem::Params p;
    p.adjustment_rate = 0.5f;     // half the gap per tick
    p.max_step_s      = 100.0f;   // disable cap for this test
    ClockSyncSystem cs(w, p);

    cs.tick(w.registry(), w.context(), 0.1f);
    CHECK(w.clock_offset_s() == doctest::Approx(2.5f).epsilon(0.001));
    CHECK(cs.adjustments_applied() == 1u);
    CHECK(cs.observations() == 1u);
}

TEST_CASE("ClockSyncSystem: peer behind us pulls offset negative") {
    World w(WorldConfig{});
    w.init();

    // Peer reports sync_time = -3.0 → gap = -3.0 from our 0.
    seed_peer(w.neighbour_table(), -3.0f);

    ClockSyncSystem::Params p;
    p.adjustment_rate = 1.0f;     // snap to mean
    p.max_step_s      = 100.0f;
    ClockSyncSystem cs(w, p);

    cs.tick(w.registry(), w.context(), 0.1f);
    CHECK(w.clock_offset_s() == doctest::Approx(-3.0f).epsilon(0.001));
}

TEST_CASE("ClockSyncSystem: mean of multiple peers determines the step") {
    World w(WorldConfig{});
    w.init();

    seed_peer(w.neighbour_table(), 4.0f);
    seed_peer(w.neighbour_table(), 6.0f);
    seed_peer(w.neighbour_table(), 8.0f);
    // Mean = 6.0; rate = 1.0 → offset becomes +6.0 in one tick.

    ClockSyncSystem::Params p;
    p.adjustment_rate = 1.0f;
    p.max_step_s      = 100.0f;
    ClockSyncSystem cs(w, p);
    cs.tick(w.registry(), w.context(), 0.1f);

    CHECK(w.clock_offset_s() == doctest::Approx(6.0f).epsilon(0.001));
    CHECK(cs.observations() == 3u);
}

TEST_CASE("ClockSyncSystem: max_step_s caps runaway adjustment") {
    World w(WorldConfig{});
    w.init();
    seed_peer(w.neighbour_table(), 1000.0f);   // huge gap

    ClockSyncSystem::Params p;
    p.adjustment_rate = 1.0f;
    p.max_step_s      = 0.5f;
    ClockSyncSystem cs(w, p);
    cs.tick(w.registry(), w.context(), 0.1f);

    CHECK(w.clock_offset_s() == 0.5f);   // capped
}

TEST_CASE("ClockSyncSystem: adjustment_rate=0 disables the system") {
    World w(WorldConfig{});
    w.init();
    seed_peer(w.neighbour_table(), 100.0f);

    ClockSyncSystem::Params p;
    p.adjustment_rate = 0.0f;
    ClockSyncSystem cs(w, p);
    for (int i = 0; i < 20; ++i) cs.tick(w.registry(), w.context(), 0.1f);

    CHECK(w.clock_offset_s() == 0.0f);
    CHECK(cs.adjustments_applied() == 0u);
}

TEST_CASE("ClockSyncSystem: converges toward consensus across many ticks") {
    World w(WorldConfig{});
    w.init();

    // Steady-state target: peers reporting sync_time = 10.0 consistently.
    seed_peer(w.neighbour_table(), 10.0f);

    ClockSyncSystem::Params p;
    p.adjustment_rate = 0.2f;
    p.max_step_s      = 100.0f;
    ClockSyncSystem cs(w, p);

    for (int i = 0; i < 60; ++i) cs.tick(w.registry(), w.context(), 0.1f);

    // After 60 ticks at rate 0.2, we should be very close to 10.0 (the
    // geometric tail leaves <0.001 remaining).
    CHECK(w.clock_offset_s() == doctest::Approx(10.0f).epsilon(0.01));
}

TEST_CASE("World::synced_time_s == elapsed_time_s + clock_offset_s") {
    World w(WorldConfig{});
    w.init();
    w.set_clock_offset_s(2.5f);
    // context.elapsed_time_s starts at 0 — tick once at 0.1 s.
    w.tick();
    CHECK(w.synced_time_s() == doctest::Approx(w.context().elapsed_time_s + 2.5f));
}

TEST_CASE("ClockSyncSystem: SystemDescriptor advertises NeighbourTable read") {
    World w(WorldConfig{});
    w.init();
    ClockSyncSystem cs(w);
    const auto d = cs.describe();
    CHECK(d.name == "ClockSyncSystem");
    CHECK(d.reads_components.empty());
    CHECK(d.writes_components.empty());

    bool found = false;
    for (auto r : d.reads_resources) {
        if (r == mith::ResourceID::NeighbourTable) { found = true; break; }
    }
    CHECK(found);
}
