#include "doctest.h"

#include "mith/comms/serial_framing.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace sf = mith::serial_framing;

namespace {

std::vector<std::vector<std::uint8_t>> decode_all(const std::uint8_t* bytes,
                                                   std::size_t n) {
    sf::Parser parser;
    std::vector<std::vector<std::uint8_t>> frames;
    parser.feed(bytes, n, [&frames](const std::uint8_t* f, std::size_t flen) {
        frames.emplace_back(f, f + flen);
    });
    return frames;
}

} // namespace

TEST_CASE("serial_framing: round-trip a single payload") {
    const std::uint8_t payload[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    std::uint8_t buf[64];
    const std::size_t n = sf::encode(payload, sizeof payload, buf, sizeof buf);
    REQUIRE(n == sf::FRAME_HEADER_BYTES + sizeof payload);

    // Inspect header.
    CHECK(buf[0] == sf::SYNC_BYTE_1);
    CHECK(buf[1] == sf::SYNC_BYTE_2);
    CHECK(buf[2] == sizeof payload);
    CHECK(buf[3] == 0u);

    // Round-trip.
    auto frames = decode_all(buf, n);
    REQUIRE(frames.size() == 1u);
    REQUIRE(frames[0].size() == sizeof payload);
    CHECK(std::memcmp(frames[0].data(), payload, sizeof payload) == 0);
}

TEST_CASE("serial_framing: encode returns 0 on capacity overflow") {
    const std::uint8_t payload[] = {0x01, 0x02};
    std::uint8_t tiny[3];
    CHECK(sf::encode(payload, sizeof payload, tiny, sizeof tiny) == 0u);
}

TEST_CASE("serial_framing: encode rejects oversize payloads") {
    std::vector<std::uint8_t> giant(sf::MAX_PAYLOAD_BYTES + 1, 0xAA);
    std::vector<std::uint8_t> out(giant.size() + 16);
    CHECK(sf::encode(giant.data(), giant.size(), out.data(), out.size()) == 0u);
}

TEST_CASE("serial_framing: parser handles multiple frames in one feed") {
    std::uint8_t buf[128];
    std::size_t off = 0;

    const std::uint8_t a[] = {0xAA, 0xBB};
    const std::uint8_t b[] = {0x11, 0x22, 0x33};
    off += sf::encode(a, sizeof a, buf + off, sizeof buf - off);
    off += sf::encode(b, sizeof b, buf + off, sizeof buf - off);

    const auto frames = decode_all(buf, off);
    REQUIRE(frames.size() == 2u);
    CHECK(std::memcmp(frames[0].data(), a, sizeof a) == 0);
    CHECK(std::memcmp(frames[1].data(), b, sizeof b) == 0);
}

TEST_CASE("serial_framing: parser drops leading garbage and resyncs") {
    std::uint8_t buf[64];
    const std::uint8_t payload[] = {0x77, 0x88};
    const std::size_t n = sf::encode(payload, sizeof payload, buf, sizeof buf);

    // Prepend 5 garbage bytes.
    std::vector<std::uint8_t> stream = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA};
    stream.insert(stream.end(), buf, buf + n);

    sf::Parser parser;
    std::vector<std::vector<std::uint8_t>> frames;
    parser.feed(stream.data(), stream.size(),
        [&frames](const std::uint8_t* f, std::size_t flen) {
            frames.emplace_back(f, f + flen);
        });

    REQUIRE(frames.size() == 1u);
    CHECK(std::memcmp(frames[0].data(), payload, sizeof payload) == 0);
    CHECK(parser.bytes_dropped() >= 5u);
}

TEST_CASE("serial_framing: parser handles fragmented input (byte at a time)") {
    std::uint8_t buf[64];
    const std::uint8_t payload[] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6};
    const std::size_t n = sf::encode(payload, sizeof payload, buf, sizeof buf);

    sf::Parser parser;
    std::vector<std::vector<std::uint8_t>> frames;
    auto cb = [&frames](const std::uint8_t* f, std::size_t flen) {
        frames.emplace_back(f, f + flen);
    };
    for (std::size_t i = 0; i < n; ++i) {
        parser.feed(buf + i, 1u, cb);
    }

    REQUIRE(frames.size() == 1u);
    CHECK(std::memcmp(frames[0].data(), payload, sizeof payload) == 0);
}

TEST_CASE("serial_framing: zero-length frame is emitted immediately") {
    std::uint8_t buf[8];
    const std::size_t n = sf::encode(nullptr, 0u, buf, sizeof buf);
    REQUIRE(n == sf::FRAME_HEADER_BYTES);

    sf::Parser parser;
    int count = 0;
    std::size_t seen_len = 999;
    parser.feed(buf, n, [&](const std::uint8_t*, std::size_t flen) {
        ++count;
        seen_len = flen;
    });
    CHECK(count == 1);
    CHECK(seen_len == 0u);
}

TEST_CASE("serial_framing: oversize length field is dropped without overflow") {
    sf::Parser parser(/*max_payload=*/16);
    // Hand-craft a header claiming length 1000.
    const std::uint8_t hdr[] = {
        sf::SYNC_BYTE_1, sf::SYNC_BYTE_2,
        0xE8, 0x03,   // 1000 LE
    };
    int frames = 0;
    parser.feed(hdr, sizeof hdr, [&frames](const std::uint8_t*, std::size_t) {
        ++frames;
    });
    CHECK(frames == 0);
    CHECK(parser.bytes_dropped() >= sf::FRAME_HEADER_BYTES);
}

TEST_CASE("serial_framing: frames_decoded counter is monotonic") {
    std::uint8_t buf[64];
    const std::uint8_t p[] = {0x01};
    const std::size_t n = sf::encode(p, sizeof p, buf, sizeof buf);

    sf::Parser parser;
    auto cb = [](const std::uint8_t*, std::size_t) {};
    parser.feed(buf, n, cb);
    parser.feed(buf, n, cb);
    parser.feed(buf, n, cb);
    CHECK(parser.frames_decoded() == 3u);
}
