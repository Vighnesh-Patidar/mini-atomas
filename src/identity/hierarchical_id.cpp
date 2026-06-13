#include "mith/identity/hierarchical_id.h"

namespace mith {

namespace {

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

constexpr std::size_t SWARM_HEX_LEN = 4;
constexpr std::size_t TOTAL_LEN     = SWARM_HEX_LEN + 1 + 36;   // "SSSS:" + UUID

} // namespace

HierarchicalID HierarchicalID::generate(SwarmID swarm) noexcept {
    return HierarchicalID{swarm, UUID::generate()};
}

std::string HierarchicalID::to_string() const {
    std::string s;
    s.reserve(TOTAL_LEN);
    s.push_back(hex_char(static_cast<std::uint8_t>((swarm_id >> 12) & 0xFu)));
    s.push_back(hex_char(static_cast<std::uint8_t>((swarm_id >>  8) & 0xFu)));
    s.push_back(hex_char(static_cast<std::uint8_t>((swarm_id >>  4) & 0xFu)));
    s.push_back(hex_char(static_cast<std::uint8_t>(swarm_id & 0xFu)));
    s.push_back(':');
    s += unit_id.to_string();
    return s;
}

std::optional<HierarchicalID> HierarchicalID::from_string(std::string_view s) noexcept {
    if (s.size() != TOTAL_LEN) return std::nullopt;
    if (s[SWARM_HEX_LEN] != ':') return std::nullopt;

    SwarmID sid = 0;
    for (std::size_t i = 0; i < SWARM_HEX_LEN; ++i) {
        const int v = hex_value(s[i]);
        if (v < 0) return std::nullopt;
        sid = static_cast<SwarmID>((sid << 4) | static_cast<SwarmID>(v));
    }

    auto uuid = UUID::from_string(s.substr(SWARM_HEX_LEN + 1));
    if (!uuid.has_value()) return std::nullopt;

    return HierarchicalID{sid, *uuid};
}

} // namespace mith

namespace std {

std::size_t hash<mith::HierarchicalID>::operator()(const mith::HierarchicalID& h) const noexcept {
    const std::size_t uuid_hash  = std::hash<mith::UUID>{}(h.unit_id);
    const std::size_t swarm_hash = static_cast<std::size_t>(h.swarm_id);
    // boost::hash_combine-style mix — matches uuid.cpp std::hash impl.
    return uuid_hash
         ^ (swarm_hash + 0x9e3779b97f4a7c15ULL + (uuid_hash << 6) + (uuid_hash >> 2));
}

} // namespace std
