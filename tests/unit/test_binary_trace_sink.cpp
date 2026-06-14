#include "doctest.h"

#include "mith/core/binary_trace_sink.h"
#include "mith/core/trace_sink.h"

#include <array>
#include <cstdint>
#include <cstring>

using mith::BinaryTraceSink;
using mith::TraceField;
using mith::TraceLevel;

namespace {

// Little-endian helpers for decoding frames.
std::uint64_t read_u64_le(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
    }
    return v;
}
std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

} // namespace

TEST_CASE("BinaryTraceSink: capacity is rounded down to a power of two") {
    BinaryTraceSink a(7);
    CHECK(a.capacity() == 4u);    // 7 → 4
    BinaryTraceSink b(16);
    CHECK(b.capacity() == 16u);
    BinaryTraceSink c(0);
    CHECK(c.capacity() == 1u);    // edge case
}

TEST_CASE("BinaryTraceSink: allocates one buffer at construction") {
    BinaryTraceSink s(8);
    CHECK(s.buffer_bytes() == 8u * BinaryTraceSink::FRAME_BYTES);
    CHECK(s.size() == 0u);
    CHECK(s.dropped() == 0u);
    CHECK(s.total_emits() == 0u);
    CHECK(s.peek_oldest() == nullptr);
}

TEST_CASE("BinaryTraceSink: emit at TraceLevel::Off is silently dropped") {
    BinaryTraceSink s(4);
    s.emit(TraceLevel::Off, "evt");
    CHECK(s.size() == 0u);
    CHECK(s.total_emits() == 0u);
}

TEST_CASE("BinaryTraceSink: a single emit produces a decodable frame") {
    BinaryTraceSink s(4);

    std::array<TraceField, 3> fields{
        TraceField::u64("count", 42u),
        TraceField::f64("ratio", 0.5),
        TraceField::boolean("ok", true),
    };
    s.emit(TraceLevel::Info, "tick_completed", fields.data(), fields.size());

    REQUIRE(s.size() == 1u);
    const std::uint8_t* f = s.peek_oldest();
    REQUIRE(f != nullptr);

    // Header.
    CHECK(read_u64_le(f + 0) == 0u);                                   // first seq
    CHECK(f[8] == static_cast<std::uint8_t>(TraceLevel::Info));
    CHECK(f[9] == 3u);                                                  // field_count
    CHECK(read_u16_le(f + 10) == 14u);                                  // event_len = "tick_completed"
    CHECK(std::memcmp(f + 12, "tick_completed", 14) == 0);

    // First field starts after 12 + 14 = 26.
    const std::uint8_t* fp = f + 12 + 14;
    CHECK(fp[0] == static_cast<std::uint8_t>(TraceField::Kind::U64));
    CHECK(fp[1] == 5u);                                                 // "count"
    CHECK(std::memcmp(fp + 2, "count", 5) == 0);
    CHECK(read_u64_le(fp + 8) == 42u);
}

TEST_CASE("BinaryTraceSink: ring evicts oldest on overflow and bumps dropped()") {
    BinaryTraceSink s(2);                       // tiny ring
    for (int i = 0; i < 5; ++i) {
        s.emit(TraceLevel::Info, "evt");
    }
    CHECK(s.total_emits() == 5u);
    CHECK(s.size() == 2u);                      // bounded
    CHECK(s.dropped() == 3u);                   // 5 - 2

    // Oldest still in ring is seq 3 (we dropped 0,1,2). Verify seq.
    const std::uint8_t* f = s.peek_oldest();
    REQUIRE(f != nullptr);
    CHECK(read_u64_le(f + 0) == 3u);
}

TEST_CASE("BinaryTraceSink: pop_oldest walks frames in FIFO order") {
    BinaryTraceSink s(4);
    s.emit(TraceLevel::Info, "a");
    s.emit(TraceLevel::Info, "b");
    s.emit(TraceLevel::Info, "c");
    REQUIRE(s.size() == 3u);

    auto read_event_name = [](const std::uint8_t* f, char* out, std::size_t cap) {
        const std::uint16_t len = read_u16_le(f + 10);
        const std::size_t n = std::min<std::size_t>(len, cap - 1);
        std::memcpy(out, f + 12, n);
        out[n] = '\0';
    };

    char buf[8];
    read_event_name(s.peek_oldest(), buf, sizeof buf);
    CHECK(std::strcmp(buf, "a") == 0);
    REQUIRE(s.pop_oldest());

    read_event_name(s.peek_oldest(), buf, sizeof buf);
    CHECK(std::strcmp(buf, "b") == 0);
    REQUIRE(s.pop_oldest());

    read_event_name(s.peek_oldest(), buf, sizeof buf);
    CHECK(std::strcmp(buf, "c") == 0);
    REQUIRE(s.pop_oldest());

    CHECK(s.size() == 0u);
    CHECK(s.peek_oldest() == nullptr);
    CHECK_FALSE(s.pop_oldest());
}

TEST_CASE("BinaryTraceSink: long event names are truncated to MAX_NAME_LEN") {
    BinaryTraceSink s(2);
    std::string long_name(BinaryTraceSink::MAX_NAME_LEN + 100, 'x');
    s.emit(TraceLevel::Info, long_name);

    const std::uint8_t* f = s.peek_oldest();
    REQUIRE(f != nullptr);
    CHECK(read_u16_le(f + 10) == BinaryTraceSink::MAX_NAME_LEN);
}

TEST_CASE("BinaryTraceSink: emits with too many fields drop tail fields, not the event") {
    BinaryTraceSink s(2);

    // 50 fields — far more than fit. The event should still be recorded
    // with as many fields as physically fit.
    std::array<TraceField, 50> many{};
    for (std::size_t i = 0; i < many.size(); ++i) {
        many[i] = TraceField::u64("k", static_cast<std::uint64_t>(i));
    }

    s.emit(TraceLevel::Info, "evt", many.data(), many.size());
    REQUIRE(s.size() == 1u);

    const std::uint8_t* f = s.peek_oldest();
    REQUIRE(f != nullptr);
    const std::uint8_t recorded = f[9];
    CHECK(recorded > 0u);
    CHECK(recorded <= 50u);
    // Sanity: recorded * FIELD_BYTES + header + name <= FRAME_BYTES.
    CHECK(12u + 3u /*evt*/ + recorded * BinaryTraceSink::FIELD_BYTES <= BinaryTraceSink::FRAME_BYTES);
}

TEST_CASE("BinaryTraceSink: clear() empties the ring; counters preserved") {
    BinaryTraceSink s(4);
    s.emit(TraceLevel::Info, "a");
    s.emit(TraceLevel::Info, "b");
    REQUIRE(s.size() == 2u);
    REQUIRE(s.total_emits() == 2u);

    s.clear();
    CHECK(s.size() == 0u);
    CHECK(s.total_emits() == 2u);   // counter monotonic, preserved
}

TEST_CASE("BinaryTraceSink: emit sequence numbers are monotonic across the ring") {
    BinaryTraceSink s(4);
    for (int i = 0; i < 4; ++i) s.emit(TraceLevel::Info, "x");

    std::uint64_t last = 0;
    bool first = true;
    while (s.size() > 0) {
        const auto seq = read_u64_le(s.peek_oldest() + 0);
        if (!first) CHECK(seq > last);
        last = seq;
        first = false;
        s.pop_oldest();
    }
}
