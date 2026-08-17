#!/usr/bin/env bash
# Regression: audio-rate integer bit ops (ctz/clz/popCount/bitWidth, >>>)
# and d(0) zero-delay semantics.
#
# Compiles each def through BOTH compilers (clang compile, link, dlopen),
# asserts d(0) resolved to the written signal at graph build, and
# byte-compares the generated C++ across the compilers (name-normalized).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
BUILD="${TZPL_BUILD:-$HOME/tzpl-build}"

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/bitops_d0_check.x" 2>/dev/null | grep -E '^(PASS|FAIL|BITOPS)')"
echo "$OUT"
echo "$OUT" | grep -q "BITOPS D0 ALL PASS" || { echo "FAIL: bit ops / d(0) compile"; exit 1; }

# Byte parity of the generated C++ between the two compilers.
for name in rt_bitops rt_d0; do
    A="$BUILD/cpp/${name}_cpp_synth.cpp"
    B="$BUILD/cpp/${name}_x_synth.cpp"
    [ -f "$A" ] && [ -f "$B" ] || { echo "FAIL: missing generated C++ for $name"; exit 1; }
    if diff <(sed "s/${name}_cpp/N/g" "$A") <(sed "s/${name}_x/N/g" "$B") > /dev/null; then
        echo "PASS $name byte parity"
    else
        echo "FAIL $name: generated C++ differs between compilers"
        exit 1
    fi
done

echo "PASS: bit ops + d(0) regression"
