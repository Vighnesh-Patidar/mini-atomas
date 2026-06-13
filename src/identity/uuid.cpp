#include "mith/identity/uuid.h"

#include <array>
#include <cstring>
#include <random>

namespace mith {

namespace {

// UUID v4 entropy source.
//
// We read all 16 UUID bytes directly from std::random_device on every call.
// Rationale:
//
//   - mt19937 / mt19937_64 are NOT cryptographically secure. From ~625 consecutive
//     outputs an attacker can recover full internal state and predict every future
//     output. For a robot in PERIODIC rotation mode (§3.4), that's a real attack.
//
//   - std::random_device is implementation-defined, but on production platforms
//     (Linux >= 5.3, macOS, Windows, recent MinGW) it is backed by the OS CSPRNG
//     (/dev/urandom, arc4random_buf, BCryptGenRandom). v0.1 ships on those.
//
//   - v0.2 (MITH_ENABLE_AUTH) replaces this with a vendored ChaCha20 CSPRNG seeded
//     once from random_device, removing the platform-quality dependency entirely.
//
// Cost: each call hits the OS entropy pool. UUID generation is not in any hot path
// (boot-time + identity rotation), so the microseconds spent here are irrelevant.
//
// std::random_device may throw on entropy-source failure. We do not catch — if
// the OS entropy source is unavailable, the runtime cannot safely produce identity
// material and should terminate. generate() remaining noexcept means a throw here
// triggers std::terminate, which is the correct behaviour for this class of failure.
std::random_device& thread_rd() noexcept {
    thread_local std::random_device rd;
    return rd;
}

constexpr char hex_char(std::uint8_t nibble) noexcept {
    return nibble < 10
        ? static_cast<char>('0' + nibble)
        : static_cast<char>('a' + (nibble - 10));
}

constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

UUID UUID::generate() noexcept {
    auto& rd = thread_rd();
    UUID::bytes_type b{};

    // Fill all 16 bytes directly from random_device. result_type is at least
    // 32 bits (guaranteed by the standard), so we need at most 4 calls.
    using rd_type = std::random_device::result_type;
    static_assert(sizeof(rd_type) >= 4, "random_device::result_type must be >= 32 bits");
    constexpr std::size_t per_call = sizeof(rd_type);

    std::size_t filled = 0;
    while (filled < SIZE) {
        const rd_type value = rd();
        const std::size_t take =
            (per_call < (SIZE - filled)) ? per_call : (SIZE - filled);
        std::memcpy(b.data() + filled, &value, take);
        filled += take;
    }

    // RFC 4122 §4.4: set version (4) and variant (10xx)
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0Fu) | 0x40u);
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3Fu) | 0x80u);

    return UUID(b);
}

std::string UUID::to_string() const {
    // Canonical form: 8-4-4-4-12 hex chars = 32 hex + 4 hyphens = 36 chars.
    std::string s;
    s.resize(36);

    auto write_byte = [&](std::size_t out_idx, std::uint8_t byte) {
        s[out_idx]     = hex_char(static_cast<std::uint8_t>(byte >> 4));
        s[out_idx + 1] = hex_char(static_cast<std::uint8_t>(byte & 0x0Fu));
    };

    // Byte → string offset mapping for "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
    static constexpr std::size_t offsets[SIZE] = {
        0, 2, 4, 6,
        9, 11,
        14, 16,
        19, 21,
        24, 26, 28, 30, 32, 34,
    };
    for (std::size_t i = 0; i < SIZE; ++i) {
        write_byte(offsets[i], bytes_[i]);
    }
    s[8]  = '-';
    s[13] = '-';
    s[18] = '-';
    s[23] = '-';

    return s;
}

std::optional<UUID> UUID::from_string(std::string_view s) noexcept {
    if (s.size() != 36) return std::nullopt;
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
        return std::nullopt;
    }

    UUID::bytes_type b{};
    static constexpr std::size_t offsets[SIZE] = {
        0, 2, 4, 6,
        9, 11,
        14, 16,
        19, 21,
        24, 26, 28, 30, 32, 34,
    };
    for (std::size_t i = 0; i < SIZE; ++i) {
        const int hi = hex_value(s[offsets[i]]);
        const int lo = hex_value(s[offsets[i] + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        b[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return UUID(b);
}

} // namespace mith

namespace std {

std::size_t hash<mith::UUID>::operator()(const mith::UUID& u) const noexcept {
    // UUID bytes already carry high entropy. Fold the 16 bytes into
    // a size_t via a boost::hash_combine-style mix.
    const auto& b = u.bytes();
    std::size_t result = 0;
    for (std::size_t i = 0; i < mith::UUID::SIZE; i += sizeof(std::size_t)) {
        std::size_t chunk = 0;
        const std::size_t remain =
            (mith::UUID::SIZE - i < sizeof(std::size_t))
                ? (mith::UUID::SIZE - i)
                : sizeof(std::size_t);
        std::memcpy(&chunk, b.data() + i, remain);
        result ^= chunk + 0x9e3779b97f4a7c15ULL + (result << 6) + (result >> 2);
    }
    return result;
}

} // namespace std
