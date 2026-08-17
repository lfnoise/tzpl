#!/usr/bin/env bash
# Regression: the pink noise family (pink / pinkf / pinkfe).
#
# Compiles pink through BOTH compilers (byte parity of the generated C++),
# pinkf/pinkfe through synthc, renders 30 s of each, and checks the
# spectra against 1/f: residual slope ~0, no dip near fs/5 (pink), no HF
# collapse (pinkf -- the historical stale-state bug gave -3.4 dB @10k).
# The spectral check needs a python3 with numpy.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
BUILD="${TZPL_BUILD:-$HOME/tzpl-build}"

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

PY=""
for cand in python3 python3.12 python3.11; do
    if command -v "$cand" > /dev/null && "$cand" -c 'import numpy' 2>/dev/null; then
        PY="$cand"
        break
    fi
done
[ -n "$PY" ] || { echo "FAIL: no python3 with numpy found (needed for the spectral check)"; exit 1; }

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/pink_check.x" 2>/dev/null | grep -E '^(PASS|FAIL|RENDERED|PINK)')"
echo "$OUT"
echo "$OUT" | grep -q "PINK COMPILE ALL PASS" || { echo "FAIL: pink family compile"; exit 1; }

# Byte parity of pink's generated C++ between the two compilers.
A="$BUILD/cpp/pink_reg_cpp_synth.cpp"
B="$BUILD/cpp/pink_reg_x_synth.cpp"
[ -f "$A" ] && [ -f "$B" ] || { echo "FAIL: missing generated C++ for pink_reg"; exit 1; }
if diff <(sed 's/pink_reg_cpp/N/g' "$A") <(sed 's/pink_reg_x/N/g' "$B") > /dev/null; then
    echo "PASS pink byte parity"
else
    echo "FAIL pink: generated C++ differs between compilers"
    exit 1
fi

"$PY" "$SCRIPTS/pink_spectrum_check.py" \
    /tmp/tzpl_pink_reg.wav /tmp/tzpl_pinkf_reg.wav /tmp/tzpl_pinkfe_reg.wav \
    || { echo "FAIL: pink spectral regression"; exit 1; }

echo "PASS: pink noise family regression"
