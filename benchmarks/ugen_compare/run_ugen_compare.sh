#!/bin/bash
# run_ugen_compare.sh -- our common_ugens.x / filters.x ugens against their
# tensor-ugens references, for accuracy and speed (block engine plan M7).
#
#   bash benchmarks/ugen_compare/run_ugen_compare.sh [--check] [case-substring]
#
# --check runs the accuracy comparison only and exits 1 on a regression.
# Generates the plugins with ugen_cases.x (tzpl_app), compiles them with the
# flags the bridge uses, builds bench_ugen_compare in build-bench/ (Release,
# TZPL_BUILD_BENCHMARKS=ON) and runs it. Apple only, like tensor_ugens_ref.
# Env: TZPL_BUILD_DIR (default build/) for tzpl_app; UC_WORK (default
# /tmp/ugen_compare) for the generated sources and dylibs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
WORK="${UC_WORK:-/tmp/ugen_compare}"
BENCH_BUILD="$ROOT/build-bench"
CHECK=""
if [ "${1:-}" = "--check" ]; then CHECK="--check"; shift; fi
FILTER="${1:-}"

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
rm -rf "$WORK/cpp" "$WORK/dylib"
mkdir -p "$WORK/cpp" "$WORK/dylib"

UC_OUT="$WORK/cpp" UC_FILTER="$FILTER" "$APP" --nogui --no-audio \
    -I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" \
    "$ROOT/benchmarks/ugen_compare/ugen_cases.x" | grep -v '^wrote' || true

# The bridge's compile and link flags (bridge/src/tzpl_synth_build.cpp).
compile() {
    local src="$1" name
    name="$(basename "$src" .cpp)"
    clang -x c++ -arch arm64 -std=c++23 -stdlib=libc++ -O3 -ffast-math -fno-finite-math-only \
        -I "$ROOT/shared" -dynamiclib -undefined dynamic_lookup -framework Accelerate \
        -o "$WORK/dylib/$name.dylib" "$src" || echo "FAIL compile $name"
}
export -f compile
export ROOT WORK
ls "$WORK"/cpp/*.cpp | xargs -P "$(sysctl -n hw.ncpu)" -I{} bash -c 'compile {}'

if [ ! -f "$BENCH_BUILD/CMakeCache.txt" ]; then
    cmake -S "$ROOT" -B "$BENCH_BUILD" -DCMAKE_BUILD_TYPE=Release -DTZPL_BUILD_BENCHMARKS=ON > /dev/null
fi
cmake --build "$BENCH_BUILD" --target bench_ugen_compare -j "$(sysctl -n hw.ncpu)" > /dev/null
"$BENCH_BUILD/benchmarks/ugen_compare/bench_ugen_compare" $CHECK "$WORK/dylib" $FILTER
