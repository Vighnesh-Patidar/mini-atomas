// flocking_demo_3d — 20 robots in a SimBus running BeaconSystem +
// FlockingSystem + KinematicsSystem in full 3D.
//
// Robots start scattered around a cube and accelerate toward a fixed
// goal point at the origin while the Reynolds rules cluster them.
// Same JSON-line protocol as the 2D demo, with z added to each entry:
//
//   {"tick": N, "robots": [{"id": "...", "x": ..., "y": ..., "z": ...}, ...]}
//
// Pipe into tools/visualiser/visualise3d.py to render an animated GIF.

#include "mith/comms/beacon_system.h"
#include "mith/core/builtin_components.h"
#include "mith/core/json_writer.h"
#include "mith/core/registry.h"
#include "mith/core/world.h"
#include "mith/sim/sim_bus.h"
#include "mith/systems/flocking_system.h"
#include "mith/systems/kinematics_system.h"

#include <cstdio>
#include <memory>
#include <random>
#include <vector>

int main() {
    using namespace mith;

    sim::SimBusConfig bus_cfg;
    bus_cfg.tick_rate_hz = 20.0f;
    bus_cfg.comm_range_m = 40.0f;
    sim::SimBus bus(bus_cfg);

    constexpr int N_ROBOTS = 20;
    constexpr int N_TICKS  = 400;   // 20 s at 20 Hz — enough for a tight cluster

    std::vector<std::unique_ptr<World>> worlds;
    worlds.reserve(N_ROBOTS);

    std::mt19937 rng(2026);
    std::uniform_real_distribution<float> pos_jitter(-8.0f, 8.0f);
    std::uniform_real_distribution<float> vinit(-1.5f, 1.5f);

    FlockingSystem::Params flock_p;
    flock_p.separation_radius_m = 2.5f;
    flock_p.separation_weight   = 2.0f;
    flock_p.alignment_weight    = 0.7f;
    flock_p.cohesion_weight     = 0.8f;
    flock_p.max_speed_mps       = 4.0f;

    for (int i = 0; i < N_ROBOTS; ++i) {
        auto w = bus.create_world(SwarmID{1});

        w->register_system(std::make_unique<BeaconSystem>(*w));
        w->register_system(std::make_unique<FlockingSystem>(*w, flock_p));
        w->register_system(std::make_unique<KinematicsSystem>());

        w->init();

        // Scatter inside a 16m cube centred on origin, with random
        // initial 3D velocity.
        auto& pos = w->registry().get<PositionComponent>(w->self_id());
        pos.x = pos_jitter(rng);
        pos.y = pos_jitter(rng);
        pos.z = pos_jitter(rng);

        auto& vel = w->registry().get<VelocityComponent>(w->self_id());
        vel.vx = vinit(rng);
        vel.vy = vinit(rng);
        vel.vz = vinit(rng);

        worlds.push_back(std::move(w));
    }

    for (int tick = 0; tick <= N_TICKS; ++tick) {
        JsonWriter w;
        w.begin_object();
        w.key("tick");
        w.write_u64(static_cast<std::uint64_t>(tick));
        w.key("robots");
        w.begin_array();
        for (const auto& world : worlds) {
            const auto& pos =
                world->registry().get<PositionComponent>(world->self_id());
            w.begin_object();
            w.key("id"); w.write_string(world->identity().to_string());
            w.key("x");  w.write_f64(static_cast<double>(pos.x));
            w.key("y");  w.write_f64(static_cast<double>(pos.y));
            w.key("z");  w.write_f64(static_cast<double>(pos.z));
            w.end_object();
        }
        w.end_array();
        w.end_object();
        w.newline();

        const auto& line = w.str();
        std::fwrite(line.data(), 1, line.size(), stdout);

        if (tick < N_TICKS) bus.advance(1);
    }

    return 0;
}
