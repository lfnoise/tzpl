#!/usr/bin/env bash
# Non-flat (AoS) voicer tests: byte-parity between both compilers across the
# control-flow voice-body corpus, plus live behavioral checks (seeded distinct
# per-voice init draws, init-rate state surviving noteOn, per-note seq tables
# starting on pattern[0]). Runs LIVE audio briefly (quiet DC test signals).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/synthc_nonflat_diff.x" 2>&1)
if ! echo "$out" | grep -qF "NONFLAT DIFF PASS"; then
    echo "$out" | grep -E "PASS|FAIL" || true
    echo "NONFLAT VOICER TEST FAIL (diff)"
    exit 1
fi

out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/nonflat_voicer_behavior.x" 2>&1)
if ! echo "$out" | grep -qF "NONFLAT BEHAVIOR PASS"; then
    echo "$out" | grep -E "PASS|FAIL" || true
    echo "NONFLAT VOICER TEST FAIL (behavior)"
    exit 1
fi
echo "NONFLAT VOICER TEST PASS"
