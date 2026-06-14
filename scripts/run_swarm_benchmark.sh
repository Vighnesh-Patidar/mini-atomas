#!/usr/bin/env bash
# Run the v1.0 1000-entity swarm benchmark and archive results.
#
# Usage:
#   scripts/run_swarm_benchmark.sh [N] [T] [box_m] [comm_range_m] [vision_radius_m]
#
# Defaults to the v1.0 §16 perf-gate shape: N=1000 worlds, T=200 ticks,
# 100 m cube, 15 m comm range, 15 m flocking vision.
#
# Output layout:
#   qa-report/swarm-benchmark/<timestamp>/
#     per-tick.csv      tick,wall_ms,mean_neighbours (CSV stream)
#     summary.log       stderr: aggregate timings + per-system breakdown
#     proc-snapshot.txt /proc/self/status snapshot at run end
#     summary.md        human-readable report (auto-generated)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
N="${1:-1000}"
T="${2:-200}"
BOX="${3:-100.0}"
COMM_R="${4:-15.0}"
VISION_R="${5:-15.0}"

TS="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="$ROOT/qa-report/swarm-benchmark/$TS"
mkdir -p "$OUT"

BIN="$ROOT/build/examples/swarm_benchmark/swarm_benchmark"
if [ ! -x "$BIN" ]; then
    echo "Building swarm_benchmark (Release)..." >&2
    cmake -B "$ROOT/build" -S "$ROOT" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$ROOT/build" --target swarm_benchmark -j"$(nproc)" >/dev/null
fi

echo "Running: N=$N T=$T box=$BOX comm_range=$COMM_R vision=$VISION_R" >&2
echo "Output dir: $OUT" >&2

t0=$(date +%s.%N)
"$BIN" "$N" "$T" "$BOX" "$COMM_R" "$VISION_R" \
    > "$OUT/per-tick.csv" \
    2> "$OUT/summary.log"
t1=$(date +%s.%N)
total_s=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')

# Take a final /proc/self/status-style snapshot of overall memory.
cat /proc/meminfo > "$OUT/proc-snapshot.txt" 2>/dev/null || true

# Derive a few headline numbers from per-tick.csv.
tick_count=$(grep -c '^[0-9]' "$OUT/per-tick.csv" || echo 0)
avg_wall=$(awk -F, 'NR>1 && $1 ~ /^[0-9]/ {sum+=$2; n++} END{if(n>0) printf "%.3f", sum/n; else print "n/a"}' "$OUT/per-tick.csv")
max_wall=$(awk -F, 'NR>1 && $1 ~ /^[0-9]/ {if($2>m) m=$2} END{printf "%.3f", m}' "$OUT/per-tick.csv")
final_mean_n=$(tail -1 "$OUT/per-tick.csv" | awk -F, '{print $3}')

cat > "$OUT/summary.md" <<EOF
# Swarm benchmark — N=$N, T=$T

- Timestamp:        \`$TS\`
- Host:             \`$(uname -srm)\`
- CPU:              \`$(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //')\`
- Cores:            $(nproc)
- Git HEAD:         \`$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo n/a)\`
- Build type:       Release

## Configuration

| Param | Value |
|---|---|
| Worlds (N)        | $N |
| Ticks (T)         | $T |
| Box side          | ${BOX} m |
| Comm range        | ${COMM_R} m |
| Flocking vision   | ${VISION_R} m |
| Tick rate         | 20 Hz (sim) |

## Headline numbers

| Metric | Value |
|---|---|
| Total wall time   | ${total_s}s |
| Ticks captured    | $tick_count |
| Mean per-tick wall| $avg_wall ms |
| Max  per-tick wall| $max_wall ms |
| Final mean N visible per robot | $final_mean_n |

## Per-system breakdown (world 0, last tick)

\`\`\`
$(grep -A 20 'per-system tick timings' "$OUT/summary.log" | tail -n +2)
\`\`\`

## Aggregate timings (from summary.log)

\`\`\`
$(grep -A 20 'aggregate' "$OUT/summary.log" | head -10)
\`\`\`

## Artifacts

- \`per-tick.csv\`        — full CSV stream (tick, wall_ms, mean_neighbours)
- \`summary.log\`         — stderr capture: aggregate + per-system + setup time
- \`proc-snapshot.txt\`   — /proc/meminfo at run end

EOF

echo "Done — total $total_s s. Artifacts in $OUT" >&2
echo "$OUT/summary.md"
