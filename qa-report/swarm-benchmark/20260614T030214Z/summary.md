# Swarm benchmark — N=500, T=100

- Timestamp:        `20260614T030214Z`
- Host:             `Linux 6.19.14+kali-amd64 x86_64`
- CPU:              `11th Gen Intel(R) Core(TM) i5-11260H @ 2.60GHz`
- Cores:            4
- Git HEAD:         `83b827c`
- Build type:       Release

## Configuration

| Param | Value |
|---|---|
| Worlds (N)        | 500 |
| Ticks (T)         | 100 |
| Box side          | 100 m |
| Comm range        | 15 m |
| Flocking vision   | 15 m |
| Tick rate         | 20 Hz (sim) |

## Headline numbers

| Metric | Value |
|---|---|
| Total wall time   | 696.75s |
| Ticks captured    | 100 |
| Mean per-tick wall| 6961.978 ms |
| Max  per-tick wall| 13276.615 ms |
| Final mean N visible per robot | 23.90 |

## Per-system breakdown (world 0, last tick)

```
  BeaconSystem          1.125 ms
  FlockingSystem        0.003 ms
  KinematicsSystem      0.000 ms
```

## Aggregate timings (from summary.log)

```
=== aggregate (N=500, T=100) ===
  total tick wall:  696197.78 ms
  per-tick min:     0.590 ms
  per-tick median:  6499.385 ms
  per-tick p95:     12125.357 ms
  per-tick max:     13276.615 ms
  RSS at end:       11844 kB
  setup time:       537.88 ms

=== per-system tick timings (world 0, last tick) ===
```

## Artifacts

- `per-tick.csv`        — full CSV stream (tick, wall_ms, mean_neighbours)
- `summary.log`         — stderr capture: aggregate + per-system + setup time
- `proc-snapshot.txt`   — /proc/meminfo at run end

