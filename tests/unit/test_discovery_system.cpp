#include "doctest.h"

#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/core/world.h"
#include "mith/identity/hierarchical_id.h"
#include "mith/systems/discovery_system.h"

using mith::DiscoveryState;
using mith::DiscoverySystem;
using mith::HierarchicalID;
using mith::StateVector;
using mith::SwarmID;
using mith::World;
using mith::WorldConfig;

namespace {

void seed_neighbours(mith::NeighbourTable& nt, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        StateVector sv;
        sv.id = HierarchicalID::generate(SwarmID{1});
        nt.upsert(sv, 0.0f);
    }
}

} // namespace

TEST_CASE("DiscoverySystem: starts in Bootstrapping with zero peers seen") {
    World w(WorldConfig{});
    w.init();
    DiscoverySystem ds(w);

    CHECK(ds.is_bootstrapping());
    CHECK_FALSE(ds.is_active());
    CHECK(ds.current_state() == DiscoveryState::Bootstrapping);
    CHECK(ds.peers_seen() == 0u);
    CHECK(ds.time_in_bootstrap_s() == 0.0f);
}

TEST_CASE("DiscoverySystem: promotes to Active when quorum reached") {
    World w(WorldConfig{});
    w.init();

    DiscoverySystem::Params p;
    p.bootstrap_quorum     = 3;
    p.bootstrap_timeout_s  = 999.0f;     // disable timeout path
    DiscoverySystem ds(w, p);

    // Two peers — below quorum, stays in bootstrap.
    seed_neighbours(w.neighbour_table(), 2);
    ds.tick(w.registry(), w.context(), 0.1f);
    CHECK(ds.is_bootstrapping());
    CHECK(ds.peers_seen() == 2u);

    // Add a third peer → quorum met on next tick.
    seed_neighbours(w.neighbour_table(), 1);
    ds.tick(w.registry(), w.context(), 0.1f);
    CHECK(ds.is_active());
    CHECK(ds.peers_seen() == 3u);
}

TEST_CASE("DiscoverySystem: promotes to Active on timeout even with zero peers") {
    World w(WorldConfig{});
    w.init();

    DiscoverySystem::Params p;
    p.bootstrap_quorum     = 5;          // unreachable in this test
    p.bootstrap_timeout_s  = 1.0f;
    DiscoverySystem ds(w, p);

    // Tick for 0.9 s — still bootstrapping.
    for (int i = 0; i < 9; ++i) ds.tick(w.registry(), w.context(), 0.1f);
    CHECK(ds.is_bootstrapping());

    // One more tick → 1.0 s elapsed, timeout triggers.
    ds.tick(w.registry(), w.context(), 0.1f);
    CHECK(ds.is_active());
}

TEST_CASE("DiscoverySystem: stays Active once promoted, even if NeighbourTable empties") {
    World w(WorldConfig{});
    w.init();

    DiscoverySystem::Params p;
    p.bootstrap_quorum    = 1;
    p.bootstrap_timeout_s = 999.0f;
    DiscoverySystem ds(w, p);

    seed_neighbours(w.neighbour_table(), 1);
    ds.tick(w.registry(), w.context(), 0.1f);
    REQUIRE(ds.is_active());

    // Age out the table (no-op equivalent for the test — just clear it).
    w.neighbour_table().clear();
    ds.tick(w.registry(), w.context(), 0.1f);
    ds.tick(w.registry(), w.context(), 0.1f);
    CHECK(ds.is_active());   // discovery state is sticky once promoted
}

TEST_CASE("DiscoverySystem: time_in_bootstrap stops accumulating after promotion") {
    World w(WorldConfig{});
    w.init();

    DiscoverySystem::Params p;
    p.bootstrap_quorum    = 1;
    p.bootstrap_timeout_s = 999.0f;
    DiscoverySystem ds(w, p);

    seed_neighbours(w.neighbour_table(), 1);
    ds.tick(w.registry(), w.context(), 0.5f);
    const float t_at_promotion = ds.time_in_bootstrap_s();
    REQUIRE(ds.is_active());

    for (int i = 0; i < 10; ++i) ds.tick(w.registry(), w.context(), 0.5f);
    CHECK(ds.time_in_bootstrap_s() == t_at_promotion);
}

TEST_CASE("DiscoverySystem: bootstrap_quorum=0 promotes on the first tick") {
    World w(WorldConfig{});
    w.init();

    DiscoverySystem::Params p;
    p.bootstrap_quorum    = 0;
    p.bootstrap_timeout_s = 999.0f;
    DiscoverySystem ds(w, p);

    ds.tick(w.registry(), w.context(), 0.1f);
    CHECK(ds.is_active());
    CHECK(ds.peers_seen() == 0u);
}

TEST_CASE("DiscoverySystem: SystemDescriptor advertises NeighbourTable as a read-resource") {
    World w(WorldConfig{});
    w.init();
    DiscoverySystem ds(w);
    const auto d = ds.describe();

    CHECK(d.name == "DiscoverySystem");
    CHECK(d.reads_components.empty());
    CHECK(d.writes_components.empty());

    bool has_neighbour_table = false;
    for (auto r : d.reads_resources) {
        if (r == mith::ResourceID::NeighbourTable) { has_neighbour_table = true; break; }
    }
    CHECK(has_neighbour_table);
}

TEST_CASE("DiscoverySystem: deterministic — identical peer schedule → identical transition tick") {
    auto run = []() {
        World w(WorldConfig{});
        w.init();
        DiscoverySystem::Params p;
        p.bootstrap_quorum = 3;
        DiscoverySystem ds(w, p);

        int promo_tick = -1;
        for (int t = 0; t < 100; ++t) {
            if (t == 5)  seed_neighbours(w.neighbour_table(), 1);
            if (t == 12) seed_neighbours(w.neighbour_table(), 1);
            if (t == 25) seed_neighbours(w.neighbour_table(), 1);
            ds.tick(w.registry(), w.context(), 0.1f);
            if (ds.is_active() && promo_tick == -1) { promo_tick = t; break; }
        }
        return promo_tick;
    };
    CHECK(run() == run());
}
