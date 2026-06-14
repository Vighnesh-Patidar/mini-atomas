# MithAtomas

Swarm robotics orchestration runtime — C++17.

> **Decentralised by default. Composable by design. Hardware-agnostic by necessity.**

See [ARCHITECTURE.md](ARCHITECTURE.md) for the authoritative design.

<table>
<tr>
<td align="center" width="50%">

<img src="docs/assets/flocking_demo.gif" alt="2D flocking — 10 robots, 200 ticks @ 20 Hz" width="100%" />

**2D — 10 robots, 5×2 grid → tight cluster.**
The mean radius around the centroid contracts ~70% (6.87 m → 2.10 m) across 10 s of sim time. Reynolds rules pulling the flock together via cohesion + alignment; separation keeps them from overlapping.

</td>
<td align="center" width="50%">

<img src="docs/assets/flocking_demo_3d.gif" alt="3D flocking — 20 robots, 400 ticks @ 20 Hz" width="100%" />

**3D — 20 robots, scattered cube → spherical cluster.**
Same `BeaconSystem` + `FlockingSystem` + `KinematicsSystem`, full 3-axis motion. 20 robots seeded inside a 16 m cube collapse into a coherent sphere of motion in 20 s. Reproduce with `flocking_demo_3d | visualise3d.py`.

</td>
</tr>
</table>

Frames captured live; rendered by `tools/visualiser/visualise.py` (2D) and `tools/visualiser/visualise3d.py` (3D). Reproduce with [the demo commands](#run-the-flocking-demo). Full 2D snapshot grid at `docs/assets/flocking_demo.png`.

---

## Status — v0.2 feature complete

| Area | What ships |
|---|---|
| Identity | `UUID` (RFC 4122 v4), `HierarchicalID`, **Ed25519** sign/verify via vendored TweetNaCl, **ChaCha20 CSPRNG** (RFC 8439, per-thread, replaces `std::random_device`-per-call), **identity rotation** (`PERIODIC` + `EVENT_DRIVEN` + `IdentityCertificate` continuity chain signed by previous key) |
| ECS | `EntityRegistry` (registration policies, view, snapshot, `ComponentOrigin` tag, sink-wired audit), all 10 §4.4 built-in components, `BoundedQueue<T,N,Policy>` |
| Scheduling | `SystemDescriptor` two-axis hazards, `SystemScheduler` with both `Sequential` and `Parallel` modes (thread pool + hazard DAG), `last_tick_timings()` |
| Comms | `StateVector`, `Message`, `NeighbourTable`, **channel-aware transport** (`BeaconTransport` + `MessageTransport` interfaces, unified `TransportLayer` for combined impls), `BeaconSystem`, `BROADCAST_ID` semantics |
| Transports | `SimTransport` (in-process), **`UDPMulticastTransport`** (IPv4 multicast over POSIX sockets, tagged wire frames via `udp_wire`) |
| Motion | `FlockingSystem` (Reynolds), `KinematicsSystem` |
| Fault handling | **`FaultMonitorSystem`** (§13.1, §13.2) — fault counter delta → health decrement → degraded mask install with snapshot/restore + hysteresis |
| Task allocation | **`TaskAllocSystem`** (§5.3) — threshold-based, deterministic, pre-partition-merge |
| Sim | `SimClock`, `SimBus` (range-limited delivery), `SimTransport` |
| Observability | Hand-rolled JSON writer, `TraceSink` interface + `JsonTraceSink` + `NullTraceSink`, `World::dump_state()`, `component_registered` / `tick_completed` audit events |
| Runtime | `World` (config, init, tick, run, identity, transports — unified or per-channel, neighbour table, scheduler / registry forwarders, sink wiring) |
| Demo | 10-robot 2D flocking demo + matplotlib visualiser |
| Build | CMake STATIC library, install target with `find_package(mith-atomas)` config, doctest-vendored test suite, build matrix gated by `MITH_ENABLE_UDP` / `MITH_ENABLE_AUTH` |

Test suite — verified across the build matrix:

| Config | Cases | Assertions |
|---|---:|---:|
| `UDP=OFF AUTH=OFF` | 295 | 13,219 |
| `UDP=ON  AUTH=OFF` | 299 | 13,235 |
| `UDP=ON  AUTH=ON ` | 329 | 13,342 |

All green, including integration tests (`tests/integration/`) for SimBus-driven fault injection, the signed-mode rotation chain, the v0.2 full-stack lifecycle, and UDP multicast loopback.

Pre-v0.1 design phase: **9/9 questions resolved** (see [ARCHITECTURE.md §16](ARCHITECTURE.md#16-roadmap)).

v0.3 next — see the [roadmap](ARCHITECTURE.md#16-roadmap):
discovery / bootstrap protocol, clock sync, identity rotation partition-merge (epoch-leader or version-vector), serial transport, binary `TraceSink`, signed-mode beacons end-to-end, 3D motion stack hardening.

---

## Build

```sh
cmake -B build -S .
cmake --build build -j$(nproc)
```

## Test

```sh
cd build && ctest --output-on-failure
# Or the binary directly for the doctest summary:
./build/tests/mith_unit_tests
```

Single test case:

```sh
./build/tests/mith_unit_tests -tc='Parallel mode: hazardous systems run in lex order'
```

Stress (re-run the whole suite N times — catches flaky entropy / timing):

```sh
./build/tests/mith_unit_tests --count=100
```

List every case:

```sh
./build/tests/mith_unit_tests --list-test-cases
```

## Run the flocking demo

**2D — 10 robots:**

```sh
./build/examples/flocking_demo/flocking_demo | python3 tools/visualiser/visualise.py
```

200 ticks at 20 Hz (10 s sim time). Robots start in a 5×2 grid; emit one JSON object per tick describing each robot's `(x, y)`; the Python visualiser animates the scatter live.

**3D — 20 robots:**

```sh
./build/examples/flocking_demo_3d/flocking_demo_3d | python3 tools/visualiser/visualise3d.py
```

400 ticks at 20 Hz (20 s). Scattered cube → coherent sphere. Same JSON-line protocol with `z` added to each entry. Pass `--save out.gif` to export an animated GIF instead of opening the matplotlib window.

Both visualisers require `matplotlib` (and `pillow` for `--save`); nothing else. For headless capture:

```sh
./build/examples/flocking_demo/flocking_demo    > /tmp/demo.jsonl
./build/examples/flocking_demo_3d/flocking_demo_3d > /tmp/demo3d.jsonl
```

## Install

```sh
cmake --install build --prefix /your/install/prefix
```

Installs `libmith.a`, headers under `include/mith/`, and CMake package config under `lib/cmake/mith-atomas/`. Downstream consumers:

```cmake
find_package(mith-atomas REQUIRED)
target_link_libraries(my_robot PRIVATE mith::mith)
```

## CMake options

| Option | Default | Purpose |
|---|---|---|
| `MITH_BUILD_EXAMPLES` | `ON`  | Build the flocking demo |
| `MITH_BUILD_TESTS`    | `ON`  | Build the test suite |
| `MITH_BUILD_SIM`      | `ON`  | Build the simulation harness (reserved — currently always on) |
| `MITH_ENABLE_UDP`     | `ON`  | Build `UDPMulticastTransport` (POSIX sockets, IPv4 multicast) |
| `MITH_ENABLE_SERIAL`  | `OFF` | Build serial transport (lands v0.3) |
| `MITH_ENABLE_AUTH`    | `OFF` | Build cryptographic identity — Ed25519 sign/verify, ChaCha20 CSPRNG, signed `IdentityCertificate` chain |

---

## Architecture pointer

[`ARCHITECTURE.md`](ARCHITECTURE.md) is the spec. Quick map:

- **§3** Identity — `HierarchicalID`, signed/unsigned modes, rotation policies
- **§4** ECS — registry, components, type system, bounded queues
- **§5** System model — two-axis hazards, scheduler modes (Sequential / Parallel)
- **§6** Action handler registry
- **§7** Comms — `StateVector`, `Message`, `NeighbourTable`, transport, broadcast semantics
- **§8** `World` — top-level runtime, lifecycle, config
- **§9** Simulation harness
- **§13** Fault tolerance + EW posture (§13.5)
- **§14** Observability — `TraceSink`, JSON writer, `dump_state`, `last_tick_timings`
- **§15** Constraints + platform tier (SoC-class — Pi, Jetson, BeagleBone)
- **§16** Roadmap

---

## Contributing

PRs welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) for setup, conventions, commit style, and what we look for in review. The [Code of Conduct](CODE_OF_CONDUCT.md) governs all community spaces — issues, PRs, discussions.

Templates:

- `.github/PULL_REQUEST_TEMPLATE.md` — auto-applied on new PRs
- `.github/ISSUE_TEMPLATE/bug_report.md`
- `.github/ISSUE_TEMPLATE/feature_request.md`
- `.github/ISSUE_TEMPLATE/question.md`

For security-relevant issues (anything touching §3.3 identity, §13.5 EW posture, or transport auth), do not file a public issue — email the maintainer directly. See [CONTRIBUTING.md](CONTRIBUTING.md#reporting-security-issues).

## License

Apache 2.0 — see [LICENSE](LICENSE).
