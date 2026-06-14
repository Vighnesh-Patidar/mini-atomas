// swarm_benchmark — the v1.0 1000-entity perf gate (§16 v1.0).
//
// Spawns N worlds under one SimBus, registers BeaconSystem +
// FlockingSystem + KinematicsSystem on each, scatters positions in a
// cube, and ticks T times. Captures:
//
//   - stdout (CSV): tick, wall_ms, mean_neighbours
//   - stderr (summary): aggregate timings, peak RSS, per-system breakdown
//                       for a representative world
//
// Usage:
//   swarm_benchmark [N] [T] [box_m] [comm_range_m] [vision_radius_m]
//
// Defaults: 1000 worlds, 200 ticks (10 s at 20 Hz), 100 m cube,
//           15 m comm range, 15 m flocking vision.

#include "mith/comms/beacon_system.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"
#include "mith/core/world.h"
#include "mith/sim/sim_bus.h"
#include "mith/systems/flocking_system.h"
#include "mith/systems/kinematics_system.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// Pull VmRSS (kB) from /proc/self/status. Returns 0 if unavailable.
std::uint64_t read_vm_rss_kb() {
    std::ifstream s("/proc/self/status");
    std::string key, unit;
    std::uint64_t kb = 0;
    while (s >> key) {
        if (key == "VmRSS:") { s >> kb >> unit; return kb; }
        s.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    using namespace mith;

    const int   N           = (argc > 1) ? std::atoi(argv[1]) : 1000;
    const int   T           = (argc > 2) ? std::atoi(argv[2]) : 200;
    const float BOX_M       = (argc > 3) ? std::stof(argv[3]) : 100.0f;
    const float COMM_R      = (argc > 4) ? std::stof(argv[4]) : 15.0f;
    const float VISION_R    = (argc > 5) ? std::stof(argv[5]) : 15.0f;

    std::fprintf(stderr,
        "swarm_benchmark: N=%d T=%d box=%.1f comm_range=%.1f vision=%.1f\n",
        N, T, BOX_M, COMM_R, VISION_R);

    sim::SimBusConfig bus_cfg;
    bus_cfg.tick_rate_hz = 20.0f;
    bus_cfg.comm_range_m = COMM_R;
    sim::SimBus bus(bus_cfg);

    std::mt19937 rng(0xBEEFC0DE);
    std::uniform_real_distribution<float> pos_d(-BOX_M / 2.0f, BOX_M / 2.0f);
    std::uniform_real_distribution<float> vel_d(-1.0f, 1.0f);

    FlockingSystem::Params flock_p;
    flock_p.separation_radius_m = 2.0f;
    flock_p.separation_weight   = 1.5f;
    flock_p.alignment_weight    = 1.0f;
    flock_p.cohesion_weight     = 1.0f;
    flock_p.max_speed_mps       = 5.0f;
    flock_p.vision_radius_m     = VISION_R;

    std::vector<std::unique_ptr<World>> worlds;
    worlds.reserve(static_cast<std::size_t>(N));

    const double t_setup_start = now_ms();
    for (int i = 0; i < N; ++i) {
        auto w = bus.create_world(SwarmID{1});
        w->register_system(std::make_unique<BeaconSystem>(*w));
        w->register_system(std::make_unique<FlockingSystem>(*w, flock_p));
        w->register_system(std::make_unique<KinematicsSystem>());
        w->init();

        // Tune NeighbourTable cell size to ≈ comm range for best query
        // shape (one-cell radius covers most peers).
        w->neighbour_table().set_cell_size_m(COMM_R);

        auto& pos = w->registry().get<PositionComponent>(w->self_id());
        pos.x = pos_d(rng);
        pos.y = pos_d(rng);
        pos.z = pos_d(rng);
        auto& vel = w->registry().get<VelocityComponent>(w->self_id());
        vel.vx = vel_d(rng);
        vel.vy = vel_d(rng);
        vel.vz = vel_d(rng);

        worlds.push_back(std::move(w));
    }
    const double t_setup_ms = now_ms() - t_setup_start;
    const std::uint64_t rss_after_setup = read_vm_rss_kb();
    std::fprintf(stderr, "setup: %.2f ms, RSS %lu kB\n",
                 t_setup_ms, static_cast<unsigned long>(rss_after_setup));

    // Per-tick timing capture.
    std::vector<double> tick_ms; tick_ms.reserve(T);
    std::vector<std::size_t> mean_n_x100;  // mean neighbour count × 100 for fixed-point CSV

    std::printf("# tick,wall_ms,mean_neighbours\n");
    for (int t = 0; t < T; ++t) {
        const double t0 = now_ms();
        bus.advance(1);
        const double dt = now_ms() - t0;
        tick_ms.push_back(dt);

        // Sample mean neighbour count across all worlds (cheap).
        std::size_t total_n = 0;
        for (const auto& w : worlds) {
            total_n += w->neighbour_table().count();
        }
        const double mean_n = static_cast<double>(total_n) / static_cast<double>(N);
        mean_n_x100.push_back(static_cast<std::size_t>(mean_n * 100.0));

        std::printf("%d,%.3f,%.2f\n", t, dt, mean_n);
        std::fflush(stdout);   // stream progress when stdout is a file
    }

    const std::uint64_t rss_end = read_vm_rss_kb();
    const double total_ms = std::accumulate(tick_ms.begin(), tick_ms.end(), 0.0);
    std::sort(tick_ms.begin(), tick_ms.end());
    const double median = tick_ms[tick_ms.size() / 2];
    const double p95    = tick_ms[tick_ms.size() * 95 / 100];
    const double max_ms = tick_ms.back();
    const double min_ms = tick_ms.front();

    // Per-system timings for world 0 (representative). last_tick_timings
    // reflects the LAST tick only.
    std::fprintf(stderr, "\n=== aggregate (N=%d, T=%d) ===\n", N, T);
    std::fprintf(stderr, "  total tick wall:  %.2f ms\n", total_ms);
    std::fprintf(stderr, "  per-tick min:     %.3f ms\n", min_ms);
    std::fprintf(stderr, "  per-tick median:  %.3f ms\n", median);
    std::fprintf(stderr, "  per-tick p95:     %.3f ms\n", p95);
    std::fprintf(stderr, "  per-tick max:     %.3f ms\n", max_ms);
    std::fprintf(stderr, "  RSS at end:       %lu kB\n",
                 static_cast<unsigned long>(rss_end));
    std::fprintf(stderr, "  setup time:       %.2f ms\n", t_setup_ms);

    std::fprintf(stderr, "\n=== per-system tick timings (world 0, last tick) ===\n");
    const auto& timings = worlds.front()->scheduler().last_tick_timings();
    for (const auto& st : timings) {
        std::fprintf(stderr, "  %-20s  %.3f ms\n",
                     std::string(st.name).c_str(),
                     st.duration_us / 1000.0);
    }

    return 0;
}
