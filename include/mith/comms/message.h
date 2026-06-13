#pragma once

// Message — see ARCHITECTURE.md §7.3
//
// A directed or broadcast packet exchanged over the message channel
// (§7.1). Drained from the transport into CommBufferComponent (§4.4) for
// consuming systems. Distinct from the StateVector beacon stream (§7.2) —
// messages are explicit, typed, higher-priority traffic; beacons are
// periodic observational broadcasts.
//
// POD-style: trivially copyable, default-constructible, no allocations.
//
// BROADCAST_ID closes the gap that was open in earlier drafts of §7.3
// (where the doc referenced BROADCAST_ID but never defined it).
// Convention: SwarmID 0 is reserved for broadcast / unset. Legitimate
// deployments use SwarmID 1 and up. The nil UUID combined with any
// SwarmID indicates broadcast; BROADCAST_ID is the canonical "any swarm"
// broadcast sentinel.

#include "mith/identity/hierarchical_id.h"

#include <array>
#include <cstdint>

namespace mith {

using MessageTypeID = std::uint32_t;

namespace messages {

inline constexpr MessageTypeID TASK_BID      = 1;
inline constexpr MessageTypeID TASK_ASSIGN   = 2;
inline constexpr MessageTypeID FAULT_ALERT   = 3;
inline constexpr MessageTypeID FORMATION_CMD = 4;

// User-defined message types start here.
inline constexpr MessageTypeID CUSTOM = 0x1000;

} // namespace messages

// Sentinel HierarchicalID for broadcast messages.
//   - SwarmID 0 is reserved; legitimate swarms use 1..0xFFFE.
//   - The nil UUID (all bytes 0) is reserved by RFC 4122 and is never
//     produced by UUID::generate(); using it as the unit_id half is
//     unambiguous.
//   - BROADCAST_ID = {0, nil UUID} means "broadcast across all swarms."
inline constexpr HierarchicalID BROADCAST_ID{SwarmID{0}, UUID{}};

// Predicate: is the given recipient a broadcast target?
//   - Any HierarchicalID with a nil unit_id is considered broadcast.
//   - The swarm_id half remains meaningful — a transport may use it to
//     scope the broadcast to a specific swarm vs all swarms (v0.2 work).
inline constexpr bool is_broadcast(const HierarchicalID& id) noexcept {
    return id.unit_id.is_nil();
}

struct Message {
    static constexpr std::size_t PAYLOAD_SIZE = 128;

    HierarchicalID                            sender{};
    HierarchicalID                            recipient    = BROADCAST_ID;
    MessageTypeID                             type         = messages::CUSTOM;
    std::uint32_t                             seq          = 0;
    float                                     timestamp_s  = 0.0f;
    std::array<std::uint8_t, PAYLOAD_SIZE>    payload{};
};

} // namespace mith
