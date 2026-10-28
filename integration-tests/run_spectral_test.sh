#!/usr/bin/env bash
# Regression: spectralChain (frame layout, body write-back, overlap-add gain
# and latency, multichannel frames, frame-to-frame state, and the spec*
# helpers). spectral_check.x compiles and renders the cases; the numeric
# checks need a python3 with numpy.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

PY=""
for cand in python3 python3.12 python3.11; do
    if command -v "$cand" > /dev/null && "$cand" -c 'import numpy' 2>/dev/null; then
        PY="$cand"
        break
    fi
done
[ -n "$PY" ] || { echo "FAIL: no python3 with numpy found (needed for the spectral checks)"; exit 1; }

OUTDIR="$(mktemp -d "${TMPDIR:-/tmp}/tzpl_spectral.XXXXXX")"
trap 'rm -rf "$OUTDIR"' EXIT

# Deterministic RNG, so the noise-driven checks see the same signal every run.
export TZPL_RNG_SEED=0x5eed
OUT="$(SPECTRAL_DIR="$OUTDIR" "$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/spectral_check.x" 2>/dev/null | grep -E '^(PASS|FAIL|SPECTRAL)')" || true
echo "$OUT"
echo "$OUT" | grep -q "SPECTRAL RENDER ALL PASS" || { echo "FAIL: spectral compile/render"; exit 1; }
echo "$OUT" | grep -q "^FAIL" && { echo "FAIL: spectral compile checks"; exit 1; }

"$PY" -I "$SCRIPTS/spectral_check.py" "$OUTDIR" || { echo "FAIL: spectral regression"; exit 1; }
echo "PASS: spectralChain regression"
