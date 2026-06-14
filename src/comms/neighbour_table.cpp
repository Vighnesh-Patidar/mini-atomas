#include "mith/comms/neighbour_table.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mith {

NeighbourTable::CellKey
NeighbourTable::cell_for_(const PositionComponent& p) const noexcept {
    if (cell_size_m_ <= 0.0f) return CellKey{0, 0, 0};
    const float inv = 1.0f / cell_size_m_;
    return CellKey{
        static_cast<std::int32_t>(std::floor(p.x * inv)),
        static_cast<std::int32_t>(std::floor(p.y * inv)),
        static_cast<std::int32_t>(std::floor(p.z * inv)),
    };
}

void NeighbourTable::rebuild_grid_() {
    grid_.clear();
    if (cell_size_m_ <= 0.0f) return;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        grid_[cell_for_(entries_[i].position)].push_back(i);
    }
}

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
        const std::size_t idx = entries_.size();
        entries_.push_back(e);
        if (cell_size_m_ > 0.0f) {
            grid_[cell_for_(e.position)].push_back(idx);
        }
    } else {
        const auto prev_cell = cell_for_(it->position);
        it->position     = sv.position;
        it->velocity     = sv.velocity;
        it->health       = sv.health;
        it->role         = sv.role;
        it->state        = sv.state;
        it->last_seen_s  = current_time_s;
        it->rssi         = rssi;
        it->sync_time_s  = sv.sync_time_s;

        if (cell_size_m_ > 0.0f) {
            const auto new_cell = cell_for_(it->position);
            if (new_cell != prev_cell) {
                const std::size_t idx =
                    static_cast<std::size_t>(it - entries_.begin());
                auto& old_bucket = grid_[prev_cell];
                old_bucket.erase(
                    std::remove(old_bucket.begin(), old_bucket.end(), idx),
                    old_bucket.end());
                if (old_bucket.empty()) grid_.erase(prev_cell);
                grid_[new_cell].push_back(idx);
            }
        }
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
    const auto removed = before - entries_.size();
    total_evictions_ += static_cast<std::uint64_t>(removed);
    if (removed > 0) {
        // Cell indices into entries_ are stale — rebuild.
        rebuild_grid_();
    }
}

void NeighbourTable::clear() noexcept {
    entries_.clear();
    grid_.clear();
}

void NeighbourTable::for_each_within(const PositionComponent& centre,
                                      float radius_m,
                                      const Visitor& cb) const {
    if (!cb) return;
    const float r_sq = radius_m * radius_m;

    // Fallback: index disabled or grid empty (e.g. all entries at origin).
    if (cell_size_m_ <= 0.0f || grid_.empty()) {
        for (const auto& e : entries_) {
            const float dx = e.position.x - centre.x;
            const float dy = e.position.y - centre.y;
            const float dz = e.position.z - centre.z;
            if (dx*dx + dy*dy + dz*dz <= r_sq) cb(e);
        }
        return;
    }

    // Determine the inclusive cell range covering the query sphere.
    const auto centre_cell = cell_for_(centre);
    const int  span = static_cast<int>(std::ceil(radius_m / cell_size_m_));
    for (int dz = -span; dz <= span; ++dz) {
        for (int dy = -span; dy <= span; ++dy) {
            for (int dx = -span; dx <= span; ++dx) {
                CellKey key{
                    centre_cell[0] + dx,
                    centre_cell[1] + dy,
                    centre_cell[2] + dz,
                };
                auto it = grid_.find(key);
                if (it == grid_.end()) continue;
                for (std::size_t idx : it->second) {
                    const auto& e = entries_[idx];
                    const float ex = e.position.x - centre.x;
                    const float ey = e.position.y - centre.y;
                    const float ez = e.position.z - centre.z;
                    if (ex*ex + ey*ey + ez*ez <= r_sq) cb(e);
                }
            }
        }
    }
}

void NeighbourTable::set_cell_size_m(float size_m) {
    cell_size_m_ = (size_m < 0.0f) ? 0.0f : size_m;
    rebuild_grid_();
}

std::size_t NeighbourTable::occupied_cells() const noexcept {
    return grid_.size();
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
