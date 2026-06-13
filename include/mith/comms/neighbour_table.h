#pragma once

// NeighbourTable — see ARCHITECTURE.md §7.4
//
// Side-table of stale-by-default observations of other robots, populated
// from StateVector beacons received by BeaconSystem (§5.3). Each entry
// represents what THIS robot last heard about a neighbour — NOT
// authoritative state.
//
// Neighbours are intentionally NOT modeled as ECS entities (Pre-v0.1 #1
// resolution, §3.2): stale-by-default semantics, last-seen ages, RSSI,
// and provenance don't fit cleanly as per-component fields. The side-
// table model keeps the "I authoritatively own this" / "I think this is
// what someone else is doing" consistency distinction explicit.
//
// Concurrency: NOT thread-safe. Access is serialised by the §5.1
// scheduler hazard graph — NeighbourTable is a ResourceID, and systems
// declare it as a read or write resource in their SystemDescriptor.
//
// Storage: vector for cache-friendly iteration (FlockingSystem +
// TaskAllocSystem read this every tick). At v0.1 swarm sizes (10s to
// low hundreds), linear scan for upsert lookup is fine. A spatial index
// lands in v0.3 (§16) when 1000-entity benchmarks demand it.

#include "mith/comms/state_vector.h"
#include "mith/core/builtin_components.h"
#include "mith/identity/hierarchical_id.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace mith {

// NaN sentinel for "RSSI unknown" — used by transports that don't expose
// signal strength (sim transports, lossless links). Compare with
// std::isnan(), not equality (NaN != NaN).
inline const float UNKNOWN_RSSI = std::numeric_limits<float>::quiet_NaN();

class NeighbourTable {
public:
    struct Entry {
        HierarchicalID          hid;
        PositionComponent       position;
        VelocityComponent       velocity;
        HealthComponent         health;
        RoleComponent           role;
        BehaviourStateComponent state;
        float                   last_seen_s = 0.0f;     // local observation time
        float                   rssi        = UNKNOWN_RSSI;
    };

    NeighbourTable() = default;

    // Insert or update the entry for sv.id. `current_time_s` records our
    // local observation time (typically SwarmContext::elapsed_time_s).
    // `rssi` defaults to UNKNOWN_RSSI (NaN) — transports that measure
    // signal strength pass it through.
    void upsert(const StateVector& sv,
                float              current_time_s,
                float              rssi = UNKNOWN_RSSI);

    // Drop entries whose last_seen_s is older than (current_time_s -
    // timeout_s). Typical timeout is 5x the beacon interval (so a robot
    // missing 5 consecutive beacons is forgotten).
    void age_out(float current_time_s, float timeout_s);

    // Remove all entries. Counters preserved for observability.
    void clear() noexcept;

    std::size_t count() const noexcept;
    bool        empty() const noexcept;

    // Lookup by HID. Returns nullptr if not present. The returned pointer
    // is invalidated by the next upsert / age_out / clear.
    const Entry* find(const HierarchicalID& hid) const noexcept;

    // Direct iteration over current entries (cache-friendly contiguous
    // storage). Iterators invalidated by mutation, same as std::vector.
    std::vector<Entry>::const_iterator begin() const noexcept;
    std::vector<Entry>::const_iterator end()   const noexcept;

    // Observability counters — monotonic from construction. Total
    // upsert() calls and total entries removed by age_out().
    std::uint64_t total_observations() const noexcept;
    std::uint64_t total_evictions()    const noexcept;

private:
    std::vector<Entry>::iterator       find_(const HierarchicalID& hid);
    std::vector<Entry>::const_iterator find_(const HierarchicalID& hid) const;

    std::vector<Entry> entries_;
    std::uint64_t      total_observations_ = 0;
    std::uint64_t      total_evictions_    = 0;
};

} // namespace mith
