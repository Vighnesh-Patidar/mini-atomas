#include "mith/comms/neighbour_table.h"

#include <algorithm>

namespace mith {

void NeighbourTable::upsert(const StateVector& sv,
                             float              current_time_s,
                             float              rssi) {
    auto it = find_(sv.id);
    if (it == entries_.end()) {
        Entry e;
        e.hid          = sv.id;
        e.position     = sv.position;
        e.velocity     = sv.velocity;
        e.health       = sv.health;
        e.role         = sv.role;
        e.state        = sv.state;
        e.last_seen_s  = current_time_s;
        e.rssi         = rssi;
        e.sync_time_s  = sv.sync_time_s;
        entries_.push_back(e);
    } else {
        it->position     = sv.position;
        it->velocity     = sv.velocity;
        it->health       = sv.health;
        it->role         = sv.role;
        it->state        = sv.state;
        it->last_seen_s  = current_time_s;
        it->rssi         = rssi;
        it->sync_time_s  = sv.sync_time_s;
    }
    ++total_observations_;
}

void NeighbourTable::age_out(float current_time_s, float timeout_s) {
    const float threshold = current_time_s - timeout_s;
    const auto  before    = entries_.size();
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [threshold](const Entry& e) {
                           return e.last_seen_s < threshold;
                       }),
        entries_.end());
    total_evictions_ += static_cast<std::uint64_t>(before - entries_.size());
}

void NeighbourTable::clear() noexcept {
    entries_.clear();
}

std::size_t NeighbourTable::count() const noexcept { return entries_.size(); }
bool        NeighbourTable::empty() const noexcept { return entries_.empty(); }

const NeighbourTable::Entry*
NeighbourTable::find(const HierarchicalID& hid) const noexcept {
    auto it = find_(hid);
    if (it == entries_.end()) return nullptr;
    return &(*it);
}

std::vector<NeighbourTable::Entry>::const_iterator
NeighbourTable::begin() const noexcept { return entries_.begin(); }

std::vector<NeighbourTable::Entry>::const_iterator
NeighbourTable::end() const noexcept { return entries_.end(); }

std::uint64_t NeighbourTable::total_observations() const noexcept {
    return total_observations_;
}

std::uint64_t NeighbourTable::total_evictions() const noexcept {
    return total_evictions_;
}

std::vector<NeighbourTable::Entry>::iterator
NeighbourTable::find_(const HierarchicalID& hid) {
    return std::find_if(entries_.begin(), entries_.end(),
                        [&hid](const Entry& e) { return e.hid == hid; });
}

std::vector<NeighbourTable::Entry>::const_iterator
NeighbourTable::find_(const HierarchicalID& hid) const {
    return std::find_if(entries_.begin(), entries_.end(),
                        [&hid](const Entry& e) { return e.hid == hid; });
}

} // namespace mith
