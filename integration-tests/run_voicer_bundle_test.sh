#!/usr/bin/env bash
# Voicer bundle-allocation test: multiple noteOn commands in one bundle must
# each keep their own params and their own voice. Guards the rotating
# tie-break in Voicer::allocVoice (shared/tzpl_voicer.hpp): same-sample
# steals used to pile onto voice 0, losing every stolen note but the last.
# Runs LIVE audio briefly (quiet DC test signals).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/voicer_bundle_params.x" 2>&1)
if echo "$out" | grep -qF "VOICER BUNDLE PARAMS PASS"; then
    echo "VOICER BUNDLE TEST PASS"
else
    echo "$out" | grep -E "PASS|FAIL" || true
    echo "VOICER BUNDLE TEST FAIL"
    exit 1
fi
