#!/usr/bin/env bash
# Block-engine benchmark suite (block engine plan M9): CPU per node-second
# for each case in bench.x, rendered with --nrt (61 s minus 1 s, so start-up
# and compilation cancel out), best of REPEAT runs.
#
#   bash benchmarks/block_engine/run.sh [case...]
#
# Environment:
#   APP       the tzpl_app to run (default build/app/tzpl_app)
#   MODROOT   the source tree whose lang/modules and bridge/modules it uses
#             (default this tree). Point APP and MODROOT at another checkout,
#             e.g. a `main` worktree, to compare against it.
#   COUNT     nodes per case (default 16)
#   REPEAT    runs per case (default 3)
#   TZPL_FORMS  passed through (e.g. novgroup, noidle) to time a form off
#   WORK      scratch directory (default /tmp/tzpl_block_bench)
#   BENCH_X   another bench.x (an older build may lack an API this one uses)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${APP:-$ROOT/build/app/tzpl_app}"
MODROOT="${MODROOT:-$ROOT}"
COUNT="${COUNT:-16}"
REPEAT="${REPEAT:-3}"
WORK="${WORK:-/tmp/tzpl_block_bench}"
BENCH_X="${BENCH_X:-$ROOT/benchmarks/block_engine/bench.x}"
CASES=("$@")
[ ${#CASES[@]} -gt 0 ] || CASES=(knobs sinlpf1 sinlpf16 chorus echo pingpong bank pluck bankseq)

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
mkdir -p "$WORK"

# Wall time of one render of `secs` seconds (single silo, 64-frame blocks).
render() {
    local bench=$1 secs=$2
    local t0 t1
    t0=$(python3 -c 'import time; print(time.time())')
    BENCH=$bench COUNT=$COUNT TZPL_BUILD="$WORK/build_$bench" "$APP" --nogui \
        --nrt "$WORK/out.wav" --duration "$secs" --sample-rate 48000 --buffer-frames 64 \
        --channels 2 --silos 1 -I "$MODROOT/lang/modules" -I "$MODROOT/bridge/modules" \
        "$BENCH_X" > "$WORK/log_$bench.txt" 2>&1 \
        || { echo "render failed: $bench (see $WORK/log_$bench.txt)" >&2; return 1; }
    t1=$(python3 -c 'import time; print(time.time())')
    python3 -c "print($t1 - $t0)"
}

printf "%-10s %s\n" "case" "ms CPU per node-second (best of $REPEAT, $COUNT nodes)"
for b in "${CASES[@]}"; do
    render "$b" 1 > /dev/null   # compile once (cached afterwards)
    best=""
    for _ in $(seq "$REPEAT"); do
        short=$(render "$b" 1)
        long=$(render "$b" 61)
        ms=$(python3 -c "print('%.4f' % (($long - $short) / 60 / $COUNT * 1000))")
        best=$(python3 -c "print(min(float('$ms'), float('${best:-1e9}')))")
    done
    printf "%-10s %.4f\n" "$b" "$best"
done
