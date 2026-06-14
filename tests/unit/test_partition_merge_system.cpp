#include "doctest.h"

#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/core/world.h"
#include "mith/identity/hierarchical_id.h"
#include "mith/systems/partition_merge_system.h"
#include "mith/systems/task_alloc_system.h"

using mith::HierarchicalID;
using mith::NeighbourTable;
using mith::PartitionMergeSystem;
using mith::RoleComponent;
using mith::StateVector;
using mith::SwarmID;
using mith::TaskAllocSystem;
using mith::World;
using mith::WorldConfig;

namespace {

void add_peers(NeighbourTable& nt, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        StateVector sv;
        sv.id = HierarchicalID::generate(SwarmID{1});
        nt.upsert(sv, 0.0f);
    }
}

} // namespace

TEST_CASE("PartitionMergeSystem: idle (no count jump) → no heal events") {
    World w(WorldConfig{});
    w.init();
    PartitionMergeSystem ms(w);

    add_peers(w.neighbour_table(), 1);
    for (int i = 0; i < 20; ++i) ms.tick(w.registry(), w.context(), 0.1f);

    CHECK(ms.heal_events() == 0u);
    CHECK_FALSE(w.is_merging());
}

TEST_CASE("PartitionMergeSystem: count jump >= threshold opens the merge window") {
    World w(WorldConfig{});
    w.init();

    PartitionMergeSystem::Params p;
    p.partition_heal_threshold = 3;
    p.merge_window_s           = 1.0f;
    PartitionMergeSystem ms(w, p);

    // First tick records baseline (0 peers).
    ms.tick(w.registry(), w.context(), 0.1f);
    CHECK(ms.heal_events() == 0u);

    // Simulate a heal — 4 new peers appear at once.
    add_peers(w.neighbour_table(), 4);
    ms.tick(w.registry(), w.context(), 0.1f);

    CHECK(ms.heal_events() == 1u);
    CHECK(w.is_merging());
    CHECK(w.merge_window_remaining_s() == 1.0f);
}

TEST_CASE("PartitionMergeSystem: small count increment does NOT trigger heal") {
    World w(WorldConfig{});
    w.init();
    PartitionMergeSystem::Params p;
    p.partition_heal_threshold = 5;
    PartitionMergeSystem ms(w, p);

    add_peers(w.neighbour_table(), 1);
    ms.tick(w.registry(), w.context(), 0.1f);
    add_peers(w.neighbour_table(), 1);
    ms.tick(w.registry(), w.context(), 0.1f);
    add_peers(w.neighbour_table(), 1);
    ms.tick(w.registry(), w.context(), 0.1f);

    CHECK(ms.heal_events() == 0u);
    CHECK_FALSE(w.is_merging());
}

TEST_CASE("World::tick decays the merge window") {
    WorldConfig cfg;
    cfg.tick_rate_hz = 20.0f;   // 0.05 s per tick
    World w(cfg);
    w.init();

    w.set_merge_window_s(0.2f);
    REQUIRE(w.is_merging());

    w.tick();   // 0.05 s elapsed → window now 0.15
    CHECK(w.merge_window_remaining_s() == doctest::Approx(0.15f).epsilon(0.001));
    CHECK(w.is_merging());

    for (int i = 0; i < 5; ++i) w.tick();   // total 0.3 s → window exhausted
    CHECK(w.merge_window_remaining_s() == 0.0f);
    CHECK_FALSE(w.is_merging());
}

TEST_CASE("TaskAllocSystem: merge window collapses stability_window") {
    // Long stability window — would normally suppress rapid re-switches.
    World w(WorldConfig{});
    w.init();

    TaskAllocSystem::Params p;
    p.desired_ratios[1]   = 0.5f;
    p.desired_ratios[2]   = 0.5f;
    p.thresholds[1]       = 1.0f;
    p.thresholds[2]       = 1.0f;
    p.stability_window_s  = 999.0f;     // would otherwise block any switch
    p.stimulus_gain       = 1.0f;
    TaskAllocSystem ts(w, p);

    add_peers(w.neighbour_table(), 6);   // role 0 — strongly under-supplied in role 1+2
    // Without merge: stability_window blocks the switch.
    ts.tick(w.registry(), w.context(), 0.1f);
    CHECK(w.registry().get<RoleComponent>(w.self_id()).role == 0u);
    CHECK(ts.total_role_switches() == 0u);

    // Open the merge window — stability_window should collapse to 0.
    w.set_merge_window_s(1.0f);
    ts.tick(w.registry(), w.context(), 0.1f);
    CHECK(w.registry().get<RoleComponent>(w.self_id()).role != 0u);
    CHECK(ts.total_role_switches() == 1u);
}

TEST_CASE("PartitionMergeSystem: SystemDescriptor advertises NeighbourTable read") {
    World w(WorldConfig{});
    w.init();
    PartitionMergeSystem ms(w);
    const auto d = ms.describe();
    CHECK(d.name == "PartitionMergeSystem");

    bool found = false;
    for (auto r : d.reads_resources) {
        if (r == mith::ResourceID::NeighbourTable) { found = true; break; }
    }
    CHECK(found);
}
