#include "doctest.h"

#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/identity/hierarchical_id.h"

#include <cstdint>
#include <vector>

using mith::HierarchicalID;
using mith::NeighbourTable;
using mith::PositionComponent;
using mith::StateVector;
using mith::SwarmID;

namespace {

void add_peer(NeighbourTable& nt, float x, float y, float z, float t_s = 0.0f) {
    StateVector sv;
    sv.id         = HierarchicalID::generate(SwarmID{1});
    sv.position.x = x;
    sv.position.y = y;
    sv.position.z = z;
    nt.upsert(sv, t_s);
}

std::size_t count_within(const NeighbourTable& nt, float x, float y, float z, float r) {
    std::size_t hits = 0;
    PositionComponent c; c.x = x; c.y = y; c.z = z;
    nt.for_each_within(c, r, [&hits](const NeighbourTable::Entry&) { ++hits; });
    return hits;
}

} // namespace

TEST_CASE("Spatial index: empty table — for_each_within finds nothing") {
    NeighbourTable nt;
    CHECK(count_within(nt, 0, 0, 0, 100.0f) == 0u);
    CHECK(nt.occupied_cells() == 0u);
}

TEST_CASE("Spatial index: upsert populates the grid; clear empties it") {
    NeighbourTable nt;
    nt.set_cell_size_m(5.0f);

    add_peer(nt, 0, 0, 0);
    add_peer(nt, 7, 0, 0);    // different cell (x = 7 → cell 1)
    add_peer(nt, 0, 7, 0);    // different cell

    CHECK(nt.count() == 3u);
    CHECK(nt.occupied_cells() == 3u);

    nt.clear();
    CHECK(nt.count() == 0u);
    CHECK(nt.occupied_cells() == 0u);
}

TEST_CASE("Spatial index: for_each_within respects radius") {
    NeighbourTable nt;
    nt.set_cell_size_m(5.0f);

    add_peer(nt, 0, 0, 0);
    add_peer(nt, 3, 0, 0);     // within radius 5
    add_peer(nt, 10, 0, 0);    // outside radius 5
    add_peer(nt, 100, 0, 0);   // far outside

    CHECK(count_within(nt, 0, 0, 0, 5.0f)   == 2u);   // self-pos + 3m
    CHECK(count_within(nt, 0, 0, 0, 15.0f)  == 3u);   // also picks up 10
    CHECK(count_within(nt, 0, 0, 0, 200.0f) == 4u);   // all
}

TEST_CASE("Spatial index: 3D radius — peer placed only on the z axis") {
    NeighbourTable nt;
    nt.set_cell_size_m(5.0f);

    add_peer(nt, 0, 0, 8.0f);
    CHECK(count_within(nt, 0, 0, 0, 5.0f)  == 0u);
    CHECK(count_within(nt, 0, 0, 0, 10.0f) == 1u);
}

TEST_CASE("Spatial index: agrees with linear scan for many random peers") {
    NeighbourTable nt;
    nt.set_cell_size_m(3.0f);

    // Deterministic LCG so the test is reproducible.
    std::uint64_t s = 0xC0FFEE'1234u;
    auto rnd = [&s]() {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<float>(static_cast<std::int32_t>(s >> 32)) / 2.0e9f;
    };

    for (int i = 0; i < 200; ++i) {
        add_peer(nt, rnd() * 20.0f, rnd() * 20.0f, rnd() * 20.0f);
    }

    // Linear-scan reference.
    PositionComponent q; q.x = 4.0f; q.y = -3.0f; q.z = 1.0f;
    const float r   = 7.5f;
    const float r2  = r * r;
    std::size_t want = 0;
    for (auto it = nt.begin(); it != nt.end(); ++it) {
        const float dx = it->position.x - q.x;
        const float dy = it->position.y - q.y;
        const float dz = it->position.z - q.z;
        if (dx*dx + dy*dy + dz*dz <= r2) ++want;
    }

    CHECK(count_within(nt, q.x, q.y, q.z, r) == want);
}

TEST_CASE("Spatial index: a peer's position update migrates between cells") {
    NeighbourTable nt;
    nt.set_cell_size_m(5.0f);

    const auto hid = HierarchicalID::generate(SwarmID{1});
    StateVector sv;
    sv.id = hid;
    sv.position.x = 0.0f;
    nt.upsert(sv, 0.0f);
    REQUIRE(nt.occupied_cells() == 1u);

    // Move the same peer ~ 12 m on the x axis → different cell.
    sv.position.x = 12.0f;
    nt.upsert(sv, 0.0f);
    CHECK(nt.count() == 1u);
    CHECK(nt.occupied_cells() == 1u);

    // Old position no longer matches the query; new position does.
    CHECK(count_within(nt, 0, 0, 0, 3.0f)  == 0u);
    CHECK(count_within(nt, 12, 0, 0, 3.0f) == 1u);
}

TEST_CASE("Spatial index: age_out rebuilds the grid") {
    NeighbourTable nt;
    nt.set_cell_size_m(5.0f);
    add_peer(nt, 0, 0, 0, /*t_s=*/0.0f);
    add_peer(nt, 100, 0, 0, /*t_s=*/0.0f);
    add_peer(nt, 0, 100, 0, /*t_s=*/100.0f);

    REQUIRE(nt.count() == 3u);
    REQUIRE(nt.occupied_cells() == 3u);

    // Age out anything older than (current_time - timeout) = 95 → kills
    // the two at t=0, keeps the t=100 entry.
    nt.age_out(/*current_time_s=*/100.0f, /*timeout_s=*/5.0f);
    CHECK(nt.count() == 1u);
    CHECK(nt.occupied_cells() == 1u);

    // Lookups still work after the rebuild.
    CHECK(count_within(nt, 0, 100, 0, 1.0f) == 1u);
    CHECK(count_within(nt, 0, 0, 0, 1.0f)   == 0u);
}

TEST_CASE("Spatial index: cell_size_m = 0 disables the grid (falls back to linear scan)") {
    NeighbourTable nt;
    nt.set_cell_size_m(0.0f);

    add_peer(nt, 0, 0, 0);
    add_peer(nt, 1000, 0, 0);

    CHECK(nt.occupied_cells() == 0u);   // grid is empty
    // for_each_within must still visit candidates correctly.
    CHECK(count_within(nt, 0, 0, 0, 5.0f) == 1u);
    CHECK(count_within(nt, 0, 0, 0, 2000.0f) == 2u);
}

TEST_CASE("Spatial index: changing cell size re-bins all entries") {
    NeighbourTable nt;
    nt.set_cell_size_m(10.0f);
    add_peer(nt, 0, 0, 0);
    add_peer(nt, 7, 0, 0);     // same 10m cell
    REQUIRE(nt.occupied_cells() == 1u);

    nt.set_cell_size_m(3.0f);  // now (0..3), (3..6), (6..9) — different cells
    CHECK(nt.occupied_cells() == 2u);
}

TEST_CASE("Spatial index: query span scales correctly (large radius hits all cells)") {
    NeighbourTable nt;
    nt.set_cell_size_m(2.0f);

    // 5 peers along the x axis at integer cell boundaries.
    add_peer(nt, 0, 0, 0);
    add_peer(nt, 2, 0, 0);
    add_peer(nt, 4, 0, 0);
    add_peer(nt, 6, 0, 0);
    add_peer(nt, 8, 0, 0);

    CHECK(count_within(nt, 4, 0, 0, 100.0f) == 5u);   // span covers all
}
