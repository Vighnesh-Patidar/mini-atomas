#pragma once

// UUID v4 — RFC 4122 — see ARCHITECTURE.md §3.1
//
// Self-sovereign 128-bit identifier. Generation requires no coordination
// and produces a fresh UUID with cryptographic-strength entropy from
// std::random_device, expanded via std::mt19937_64.
//
// No exceptions thrown anywhere (per §15 design constraints). Malformed
// string input to from_string() returns std::nullopt.

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mith {

class UUID {
public:
    static constexpr std::size_t SIZE = 16;
    using bytes_type = std::array<std::uint8_t, SIZE>;

    constexpr UUID() noexcept = default;

    constexpr explicit UUID(const bytes_type& bytes) noexcept
        : bytes_(bytes) {}

    // Generate a fresh RFC 4122 v4 UUID. Thread-safe.
    static UUID generate() noexcept;

    // Parse canonical lowercase or uppercase form
    //   "xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx"  (36 chars)
    // Returns nullopt on any malformed input.
    static std::optional<UUID> from_string(std::string_view s) noexcept;

    // Canonical lowercase string form (36 chars).
    std::string to_string() const;

    constexpr const bytes_type& bytes() const noexcept { return bytes_; }

    constexpr bool is_nil() const noexcept {
        for (auto byte : bytes_) {
            if (byte != 0) return false;
        }
        return true;
    }

    // RFC 4122 version nibble (bits 48-51 of the UUID, i.e. high nibble of byte 6).
    // Returns 4 for UUIDs produced by generate().
    constexpr std::uint8_t version() const noexcept {
        return static_cast<std::uint8_t>((bytes_[6] & 0xF0u) >> 4);
    }

    // std::array::operator== and operator< are not constexpr until C++20.
    // Implement the byte comparison manually so these operators stay usable
    // in constant expressions under C++17 (the project standard).
    friend constexpr bool operator==(const UUID& a, const UUID& b) noexcept {
        for (std::size_t i = 0; i < SIZE; ++i) {
            if (a.bytes_[i] != b.bytes_[i]) return false;
        }
        return true;
    }
    friend constexpr bool operator!=(const UUID& a, const UUID& b) noexcept {
        return !(a == b);
    }
    friend constexpr bool operator<(const UUID& a, const UUID& b) noexcept {
        for (std::size_t i = 0; i < SIZE; ++i) {
            if (a.bytes_[i] != b.bytes_[i]) {
                return a.bytes_[i] < b.bytes_[i];
            }
        }
        return false;
    }
    friend constexpr bool operator>(const UUID& a, const UUID& b) noexcept {
        return b < a;
    }
    friend constexpr bool operator<=(const UUID& a, const UUID& b) noexcept {
        return !(b < a);
    }
    friend constexpr bool operator>=(const UUID& a, const UUID& b) noexcept {
        return !(a < b);
    }

private:
    bytes_type bytes_{};
};

} // namespace mith

namespace std {

template<>
struct hash<mith::UUID> {
    std::size_t operator()(const mith::UUID& u) const noexcept;
};

} // namespace std
