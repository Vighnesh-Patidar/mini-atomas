#include "doctest.h"

#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/identity/hierarchical_id.h"

#include <cmath>
#include <cstdint>

using mith::HierarchicalID;
using mith::NeighbourTable;
using mith::StateVector;
using mith::SwarmID;

namespace {

StateVector make_sv(SwarmID swarm, float x = 0.0f, float y = 0.0f) {
    StateVector sv;
    sv.id           = HierarchicalID::generate(swarm);
    sv.position.x   = x;
    sv.position.y   = y;
    return sv;
}

} // namespace

TEST_CASE("default-constructed NeighbourTable is empty") {
    NeighbourTable t;
    CHECK(t.empty());
    CHECK(t.count() == 0u);
    CHECK(t.total_observations() == 0u);
    CHECK(t.total_evictions() == 0u);
    CHECK(t.begin() == t.end());
}

TEST_CASE("upsert inserts a new entry") {
    NeighbourTable t;
    const auto sv = make_sv(SwarmID{1}, 1.0f, 2.0f);

    t.upsert(sv, /*current_time_s=*/1.0f);

    CHECK(t.count() == 1u);
    CHECK_FALSE(t.empty());
    CHECK(t.total_observations() == 1u);

    const auto* e = t.find(sv.id);
    REQUIRE(e != nullptr);
    CHECK(e->hid == sv.id);
    CHECK(e->position.x == 1.0f);
    CHECK(e->position.y == 2.0f);
    CHECK(e->last_seen_s == 1.0f);
    CHECK(std::isnan(e->rssi));   // default UNKNOWN_RSSI
}

TEST_CASE("upsert updates an existing entry in place") {
    NeighbourTable t;
    auto sv = make_sv(SwarmID{1}, 0.0f, 0.0f);

    t.upsert(sv, 1.0f);
    REQUIRE(t.count() == 1u);

    // Same id, new position + later time.
    sv.position.x = 5.0f;
    sv.position.y = 6.0f;
    t.upsert(sv, 2.5f);

    CHECK(t.count() == 1u);          // not duplicated
    CHECK(t.total_observations() == 2u);

    const auto* e = t.find(sv.id);
    REQUIRE(e != nullptr);
    CHECK(e->position.x == 5.0f);
    CHECK(e->position.y == 6.0f);
    CHECK(e->last_seen_s == 2.5f);
}

TEST_CASE("RSSI value is stored when supplied") {
    NeighbourTable t;
    const auto sv = make_sv(SwarmID{1});

    t.upsert(sv, 1.0f, /*rssi=*/-65.5f);

    const auto* e = t.find(sv.id);
    REQUIRE(e != nullptr);
    CHECK(e->rssi == doctest::Approx(-65.5f));
}

TEST_CASE("find returns nullptr for an unknown HID") {
    NeighbourTable t;
    const auto sv = make_sv(SwarmID{1});
    t.upsert(sv, 1.0f);

    const auto other = HierarchicalID::generate(SwarmID{99});
    CHECK(t.find(other) == nullptr);
}

TEST_CASE("multiple distinct neighbours coexist") {
    NeighbourTable t;
    const auto a = make_sv(SwarmID{1}, 0.0f, 0.0f);
    const auto b = make_sv(SwarmID{1}, 10.0f, 0.0f);
    const auto c = make_sv(SwarmID{1}, 0.0f, 10.0f);

    t.upsert(a, 1.0f);
    t.upsert(b, 1.0f);
    t.upsert(c, 1.0f);

    CHECK(t.count() == 3u);
    CHECK(t.find(a.id)->position.x == 0.0f);
    CHECK(t.find(b.id)->position.x == 10.0f);
    CHECK(t.find(c.id)->position.y == 10.0f);
}

TEST_CASE("age_out removes only stale entries") {
    NeighbourTable t;
    const auto a = make_sv(SwarmID{1}, 1.0f, 0.0f);
    const auto b = make_sv(SwarmID{1}, 2.0f, 0.0f);
    const auto c = make_sv(SwarmID{1}, 3.0f, 0.0f);

    t.upsert(a, /*t=*/1.0f);   // old
    t.upsert(b, /*t=*/5.0f);   // fresh
    t.upsert(c, /*t=*/10.0f);  // freshest

    // At t=11, with timeout=5 → threshold = 6. Entries with
    // last_seen_s < 6 are evicted: that's a (1.0) and b (5.0). c (10.0)
    // survives.
    t.age_out(/*current_time_s=*/11.0f, /*timeout_s=*/5.0f);

    CHECK(t.count() == 1u);
    CHECK(t.total_evictions() == 2u);
    CHECK(t.find(a.id) == nullptr);
    CHECK(t.find(b.id) == nullptr);
    REQUIRE(t.find(c.id) != nullptr);
    CHECK(t.find(c.id)->position.x == 3.0f);
}

TEST_CASE("age_out leaves all entries when nothing is stale") {
    NeighbourTable t;
    t.upsert(make_sv(SwarmID{1}, 1.0f), 5.0f);
    t.upsert(make_sv(SwarmID{1}, 2.0f), 5.0f);

    t.age_out(/*current_time_s=*/5.5f, /*timeout_s=*/1.0f);

    CHECK(t.count() == 2u);
    CHECK(t.total_evictions() == 0u);
}

TEST_CASE("age_out evicts all entries when all are stale") {
    NeighbourTable t;
    t.upsert(make_sv(SwarmID{1}, 1.0f), 1.0f);
    t.upsert(make_sv(SwarmID{1}, 2.0f), 1.0f);
    t.upsert(make_sv(SwarmID{1}, 3.0f), 1.0f);

    t.age_out(/*current_time_s=*/100.0f, /*timeout_s=*/1.0f);

    CHECK(t.empty());
    CHECK(t.total_evictions() == 3u);
}

TEST_CASE("clear empties the table but preserves counters") {
    NeighbourTable t;
    t.upsert(make_sv(SwarmID{1}), 1.0f);
    t.upsert(make_sv(SwarmID{1}), 1.0f);
    REQUIRE(t.count() == 2u);
    REQUIRE(t.total_observations() == 2u);

    t.clear();

    CHECK(t.empty());
    CHECK(t.count() == 0u);
    CHECK(t.total_observations() == 2u);   // monotonic — clear does not reset
    CHECK(t.total_evictions() == 0u);
}

TEST_CASE("iteration covers every entry exactly once") {
    NeighbourTable t;
    constexpr int N = 5;
    for (int i = 0; i < N; ++i) {
        auto sv = make_sv(SwarmID{1}, static_cast<float>(i));
        t.upsert(sv, 1.0f);
    }

    int seen = 0;
    for (const auto& e : t) {
        (void) e;
        ++seen;
    }
    CHECK(seen == N);
}

TEST_CASE("re-upserting after age_out re-inserts the entry") {
    NeighbourTable t;
    const auto sv = make_sv(SwarmID{1});
    t.upsert(sv, 1.0f);
    t.age_out(100.0f, 1.0f);
    REQUIRE(t.empty());

    t.upsert(sv, 200.0f);

    CHECK(t.count() == 1u);
    CHECK(t.total_observations() == 2u);
    CHECK(t.total_evictions() == 1u);
    const auto* e = t.find(sv.id);
    REQUIRE(e != nullptr);
    CHECK(e->last_seen_s == 200.0f);
}

TEST_CASE("upsert preserves component state from the StateVector") {
    NeighbourTable t;
    StateVector sv;
    sv.id        = HierarchicalID::generate(SwarmID{42});
    sv.position  = mith::PositionComponent{1.0f, 2.0f, 3.0f};
    sv.velocity  = mith::VelocityComponent{0.5f, -0.5f, 0.0f};
    sv.health    = mith::HealthComponent{40u};
    sv.role      = mith::RoleComponent{7u};
    sv.state     = mith::BehaviourStateComponent{3u};

    t.upsert(sv, 1.0f);
    const auto* e = t.find(sv.id);
    REQUIRE(e != nullptr);

    CHECK(e->position.x == 1.0f);
    CHECK(e->position.z == 3.0f);
    CHECK(e->velocity.vy == doctest::Approx(-0.5f));
    CHECK(e->health.value == 40u);
    CHECK(e->role.role == 7u);
    CHECK(e->state.state == 3u);
}
