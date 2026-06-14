// spatial_index_benchmark — measures the §16 v0.3 spatial-index win.
//
// Seeds the NeighbourTable with N peers in a fixed cube, then for each
// of M query points measures the wall time of
//   (a) full linear scan of the table, and
//   (b) for_each_within() with cell_size = comm_range.
// Emits a CSV row per N so the user can plot N vs ratio.

#include "mith/comms/neighbour_table.h"
#include "mith/comms/state_vector.h"
#include "mith/identity/hierarchical_id.h"

#include <chrono>
#include <cstdio>
#include <cstdint>

namespace {

float lcg() {
    static std::uint64_t s = 0xCAFE'BABE'1234'5678ull;
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<float>(static_cast<std::int32_t>(s >> 32)) / 2.0e9f;   // [-1, 1]
}

void seed(mith::NeighbourTable& nt, std::size_t n, float box_m) {
    using mith::HierarchicalID;
    using mith::StateVector;
    using mith::SwarmID;
    for (std::size_t i = 0; i < n; ++i) {
        StateVector sv;
        sv.id         = HierarchicalID::generate(SwarmID{1});
        sv.position.x = lcg() * box_m;
        sv.position.y = lcg() * box_m;
        sv.position.z = lcg() * box_m;
        nt.upsert(sv, 0.0f);
    }
}

double measure_us(const std::function<void()>& body) {
    using namespace std::chrono;
    const auto t0 = steady_clock::now();
    body();
    const auto t1 = steady_clock::now();
    return duration<double, std::micro>(t1 - t0).count();
}

} // namespace

int main() {
    using mith::NeighbourTable;
    using mith::PositionComponent;

    constexpr float BOX_M       = 100.0f;
    constexpr float COMM_R      = 8.0f;
    constexpr int   N_QUERIES   = 200;
    constexpr std::size_t SIZES[] = {64, 128, 256, 512, 1024};

    std::printf("# N, linear_us_per_query, indexed_us_per_query, ratio\n");

    for (std::size_t n : SIZES) {
        NeighbourTable nt;
        nt.set_cell_size_m(COMM_R);
        seed(nt, n, BOX_M);

        // Linear-scan baseline: iterate all + distance check inline.
        double t_lin = measure_us([&]() {
            std::size_t total = 0;
            for (int q = 0; q < N_QUERIES; ++q) {
                const float cx = lcg() * BOX_M;
                const float cy = lcg() * BOX_M;
                const float cz = lcg() * BOX_M;
                const float r2 = COMM_R * COMM_R;
                for (auto it = nt.begin(); it != nt.end(); ++it) {
                    const float dx = it->position.x - cx;
                    const float dy = it->position.y - cy;
                    const float dz = it->position.z - cz;
                    if (dx*dx + dy*dy + dz*dz <= r2) ++total;
                }
            }
            // Prevent dead-code elimination.
            std::fprintf(stderr, "%zu\n", total);
        });

        // Indexed.
        double t_idx = measure_us([&]() {
            std::size_t total = 0;
            for (int q = 0; q < N_QUERIES; ++q) {
                PositionComponent c;
                c.x = lcg() * BOX_M;
                c.y = lcg() * BOX_M;
                c.z = lcg() * BOX_M;
                nt.for_each_within(c, COMM_R,
                    [&total](const NeighbourTable::Entry&) { ++total; });
            }
            std::fprintf(stderr, "%zu\n", total);
        });

        const double lin_per = t_lin / N_QUERIES;
        const double idx_per = t_idx / N_QUERIES;
        const double ratio   = (idx_per > 0.0) ? (lin_per / idx_per) : 0.0;
        std::printf("%zu,%.2f,%.2f,%.2fx\n", n, lin_per, idx_per, ratio);
    }
    return 0;
}
