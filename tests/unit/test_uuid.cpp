#include "doctest.h"

#include "mith/identity/uuid.h"

#include <string>
#include <unordered_set>

using mith::UUID;

TEST_CASE("default-constructed UUID is nil") {
    UUID u;
    CHECK(u.is_nil());
    CHECK(u.to_string() == "00000000-0000-0000-0000-000000000000");
    CHECK(u.version() == 0u);
}

TEST_CASE("generate() conforms to RFC 4122 v4") {
    const auto u = UUID::generate();

    CHECK_FALSE(u.is_nil());
    CHECK(u.version() == 4u);

    // Variant bits: top two bits of byte 8 must be 0b10
    const auto variant_bits = u.bytes()[8] & 0xC0u;
    CHECK(variant_bits == 0x80u);
}

TEST_CASE("generate() produces unique UUIDs across many draws") {
    constexpr int N = 10000;
    std::unordered_set<UUID> seen;
    seen.reserve(N);

    for (int i = 0; i < N; ++i) {
        const auto u = UUID::generate();
        const auto inserted = seen.insert(u).second;
        REQUIRE(inserted);   // any collision is a generator bug
    }

    CHECK(seen.size() == static_cast<std::size_t>(N));
}

TEST_CASE("to_string() produces canonical 36-char lowercase form") {
    const auto u = UUID::generate();
    const auto s = u.to_string();

    REQUIRE(s.size() == 36u);
    CHECK(s[8]  == '-');
    CHECK(s[13] == '-');
    CHECK(s[18] == '-');
    CHECK(s[23] == '-');

    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        const char c = s[i];
        const bool valid_lower_hex =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        CHECK(valid_lower_hex);
    }

    // RFC 4122 v4 version nibble lives at string offset 14.
    CHECK(s[14] == '4');
    // Variant nibble at offset 19 must be 8, 9, a, or b.
    CHECK((s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b'));
}

TEST_CASE("to_string() <-> from_string() round-trips") {
    for (int i = 0; i < 256; ++i) {
        const auto original = UUID::generate();
        const auto serialised = original.to_string();
        const auto parsed = UUID::from_string(serialised);

        REQUIRE(parsed.has_value());
        CHECK(*parsed == original);
        CHECK(parsed->to_string() == serialised);
    }
}

TEST_CASE("from_string() accepts uppercase and mixed-case input") {
    const auto lower = UUID::from_string("550e8400-e29b-41d4-a716-446655440000");
    const auto upper = UUID::from_string("550E8400-E29B-41D4-A716-446655440000");
    const auto mixed = UUID::from_string("550e8400-E29B-41d4-A716-446655440000");

    REQUIRE(lower.has_value());
    REQUIRE(upper.has_value());
    REQUIRE(mixed.has_value());

    CHECK(*lower == *upper);
    CHECK(*lower == *mixed);

    // Canonical output is always lowercase.
    CHECK(upper->to_string() == "550e8400-e29b-41d4-a716-446655440000");
}

TEST_CASE("from_string() rejects malformed input") {
    SUBCASE("empty") {
        CHECK_FALSE(UUID::from_string("").has_value());
    }
    SUBCASE("garbage") {
        CHECK_FALSE(UUID::from_string("not-a-uuid").has_value());
    }
    SUBCASE("one char short") {
        CHECK_FALSE(UUID::from_string("550e8400-e29b-41d4-a716-44665544000").has_value());
    }
    SUBCASE("one char long") {
        CHECK_FALSE(UUID::from_string("550e8400-e29b-41d4-a716-4466554400000").has_value());
    }
    SUBCASE("missing hyphen") {
        CHECK_FALSE(UUID::from_string("550e8400e29b-41d4-a716-446655440000-").has_value());
    }
    SUBCASE("hyphen in wrong position") {
        CHECK_FALSE(UUID::from_string("550e840-0e29b-41d4-a716-446655440000").has_value());
    }
    SUBCASE("invalid hex char in middle") {
        CHECK_FALSE(UUID::from_string("550e8400-e29b-41d4-a716-44665544000g").has_value());
    }
    SUBCASE("all invalid hex") {
        CHECK_FALSE(UUID::from_string("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz").has_value());
    }
    SUBCASE("leading space") {
        CHECK_FALSE(UUID::from_string(" 50e8400-e29b-41d4-a716-446655440000").has_value());
    }
    SUBCASE("trailing space") {
        CHECK_FALSE(UUID::from_string("550e8400-e29b-41d4-a716-446655440000 ").has_value());
    }
}

TEST_CASE("equality and ordering are consistent") {
    const auto a = UUID::generate();
    const auto a_copy = a;
    const auto b = UUID::generate();

    CHECK(a == a_copy);
    CHECK_FALSE(a != a_copy);
    CHECK_FALSE(a < a_copy);
    CHECK(a <= a_copy);
    CHECK(a >= a_copy);

    // a and b are statistically guaranteed distinct, but assert the
    // ordering machinery is internally consistent either way.
    if (a == b) {
        CHECK_FALSE(a < b);
        CHECK_FALSE(b < a);
    } else {
        const bool a_lt_b = a < b;
        const bool b_lt_a = b < a;
        CHECK(a_lt_b != b_lt_a);
        CHECK((a != b));
    }
}

TEST_CASE("std::hash<UUID> distributes across many draws") {
    constexpr int N = 1024;
    std::unordered_set<std::size_t> hashes;
    std::hash<UUID> hasher;

    for (int i = 0; i < N; ++i) {
        hashes.insert(hasher(UUID::generate()));
    }

    // 128 bits of input → at most a handful of collisions even on 32-bit size_t.
    // Loose bound — catches a constant-output bug, not a distribution test.
    CHECK(hashes.size() >= static_cast<std::size_t>(N - 4));
}

TEST_CASE("UUID is usable as an unordered_set key") {
    std::unordered_set<UUID> ids;
    constexpr int N = 256;

    for (int i = 0; i < N; ++i) {
        ids.insert(UUID::generate());
    }

    CHECK(ids.size() == static_cast<std::size_t>(N));
}

TEST_CASE("explicit byte construction round-trips") {
    UUID::bytes_type bytes{};
    for (std::size_t i = 0; i < UUID::SIZE; ++i) {
        bytes[i] = static_cast<std::uint8_t>(i * 17u + 1u);
    }

    UUID u{bytes};
    CHECK(u.bytes() == bytes);

    const auto parsed = UUID::from_string(u.to_string());
    REQUIRE(parsed.has_value());
    CHECK(parsed->bytes() == bytes);
}
