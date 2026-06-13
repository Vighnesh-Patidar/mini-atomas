#include "doctest.h"

#include "mith/identity/hierarchical_id.h"

#include <string>
#include <unordered_set>

using mith::HierarchicalID;
using mith::SwarmID;
using mith::UUID;

TEST_CASE("default-constructed HierarchicalID is all zero") {
    HierarchicalID h;
    CHECK(h.swarm_id == 0u);
    CHECK(h.unit_id.is_nil());
}

TEST_CASE("generate() preserves the given SwarmID and produces a v4 UUID") {
    constexpr SwarmID swarm = 0x1234u;
    const auto h = HierarchicalID::generate(swarm);

    CHECK(h.swarm_id == swarm);
    CHECK_FALSE(h.unit_id.is_nil());
    CHECK(h.unit_id.version() == 4u);
}

TEST_CASE("generate() works across edge SwarmIDs") {
    constexpr SwarmID values[] = {
        SwarmID{0u}, SwarmID{1u},
        SwarmID{0x00FFu}, SwarmID{0xFF00u}, SwarmID{0xFFFFu},
    };

    for (const SwarmID sid : values) {
        const auto h = HierarchicalID::generate(sid);
        CHECK(h.swarm_id == sid);
        CHECK(h.unit_id.version() == 4u);
    }
}

TEST_CASE("to_string() produces canonical 41-char lowercase form") {
    const auto h = HierarchicalID::generate(0xABCDu);
    const auto s = h.to_string();

    REQUIRE(s.size() == 41u);
    CHECK(s.substr(0, 4) == "abcd");
    CHECK(s[4] == ':');
    CHECK(s.substr(5) == h.unit_id.to_string());
}

TEST_CASE("to_string() zero-pads SwarmID to four hex digits") {
    HierarchicalID h{SwarmID{0x0001u}, UUID{}};
    const auto s = h.to_string();
    CHECK(s.substr(0, 4) == "0001");
    CHECK(s.substr(5) == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("to_string() <-> from_string() round-trips") {
    for (int i = 0; i < 256; ++i) {
        const auto original = HierarchicalID::generate(static_cast<SwarmID>(i * 257));
        const auto serialised = original.to_string();
        const auto parsed = HierarchicalID::from_string(serialised);

        REQUIRE(parsed.has_value());
        CHECK(*parsed == original);
        CHECK(parsed->to_string() == serialised);
    }
}

TEST_CASE("from_string() accepts uppercase swarm hex and UUID parts") {
    const auto lower = HierarchicalID::from_string("00ab:550e8400-e29b-41d4-a716-446655440000");
    const auto upper = HierarchicalID::from_string("00AB:550E8400-E29B-41D4-A716-446655440000");
    const auto mixed = HierarchicalID::from_string("00Ab:550e8400-E29B-41d4-A716-446655440000");

    REQUIRE(lower.has_value());
    REQUIRE(upper.has_value());
    REQUIRE(mixed.has_value());

    CHECK(*lower == *upper);
    CHECK(*lower == *mixed);

    // Canonical output normalises to lowercase.
    CHECK(upper->to_string() == "00ab:550e8400-e29b-41d4-a716-446655440000");
}

TEST_CASE("from_string() rejects malformed input") {
    SUBCASE("empty") {
        CHECK_FALSE(HierarchicalID::from_string("").has_value());
    }
    SUBCASE("one char short") {
        CHECK_FALSE(HierarchicalID::from_string("0001:550e8400-e29b-41d4-a716-44665544000").has_value());
    }
    SUBCASE("one char long") {
        CHECK_FALSE(HierarchicalID::from_string("0001:550e8400-e29b-41d4-a716-4466554400000").has_value());
    }
    SUBCASE("wrong separator (dash)") {
        CHECK_FALSE(HierarchicalID::from_string("0001-550e8400-e29b-41d4-a716-446655440000").has_value());
    }
    SUBCASE("non-hex SwarmID") {
        CHECK_FALSE(HierarchicalID::from_string("xyz1:550e8400-e29b-41d4-a716-446655440000").has_value());
    }
    SUBCASE("invalid UUID part") {
        CHECK_FALSE(HierarchicalID::from_string("0001:not-a-real-uuid-just-aaaa-bbbbbbbbbbbb").has_value());
    }
    SUBCASE("leading whitespace") {
        CHECK_FALSE(HierarchicalID::from_string(" 001:550e8400-e29b-41d4-a716-446655440000").has_value());
    }
}

TEST_CASE("equality and ordering are lexicographic in (swarm_id, unit_id)") {
    const auto u1 = UUID::generate();
    const auto u2 = UUID::generate();

    const HierarchicalID a{SwarmID{1u}, u1};
    const HierarchicalID a_copy{SwarmID{1u}, u1};
    const HierarchicalID same_swarm_other_uuid{SwarmID{1u}, u2};
    const HierarchicalID other_swarm_same_uuid{SwarmID{2u}, u1};

    CHECK(a == a_copy);
    CHECK_FALSE(a != a_copy);
    CHECK(a <= a_copy);
    CHECK(a >= a_copy);

    // SwarmID dominates ordering.
    CHECK(a < other_swarm_same_uuid);
    CHECK_FALSE(other_swarm_same_uuid < a);
    CHECK(other_swarm_same_uuid > a);

    // Within a swarm, ordering follows unit_id.
    if (u1 < u2) {
        CHECK(a < same_swarm_other_uuid);
    } else if (u2 < u1) {
        CHECK(same_swarm_other_uuid < a);
    }
}

TEST_CASE("std::hash distinguishes by both swarm_id and unit_id") {
    const auto u = UUID::generate();
    const HierarchicalID a{SwarmID{1u}, u};
    const HierarchicalID b{SwarmID{2u}, u};

    std::hash<HierarchicalID> hasher;
    // Same UUID, different SwarmID — hashes should diverge.
    CHECK(hasher(a) != hasher(b));
}

TEST_CASE("HierarchicalID is usable as an unordered_set key") {
    constexpr int N = 256;
    std::unordered_set<HierarchicalID> ids;
    for (int i = 0; i < N; ++i) {
        ids.insert(HierarchicalID::generate(static_cast<SwarmID>(i)));
    }
    CHECK(ids.size() == static_cast<std::size_t>(N));
}

TEST_CASE("constexpr equality is usable in constant expressions") {
    constexpr UUID u{};
    constexpr HierarchicalID a{SwarmID{1u}, u};
    constexpr HierarchicalID b{SwarmID{1u}, u};
    constexpr HierarchicalID c{SwarmID{2u}, u};

    static_assert(a == b, "constexpr equality on identical IDs must hold");
    static_assert(a != c, "constexpr inequality on differing swarm must hold");
    static_assert(a < c,  "constexpr ordering must follow swarm_id first");
}
