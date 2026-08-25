#!/usr/bin/env bash
# The live proxy system (lang/modules/live*): four stages.
#   1. live.plan  -- pure planning layer (scans, op ordering, quant math)
#   2. live.proxy -- functional choreography, engine stopped (bundles inline)
#   3. live.pattern -- pattern proxies (auto-derived Voice, player lifecycle)
#   4. NRT render -- a full session rendered offline; output must be audible
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

run_stage() {  # <label> <script> <pass-marker>
    local OUT
    OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/$2" 2>/dev/null \
          | grep -E '^(PASS|FAIL|LIVE)')"
    echo "$OUT"
    if ! echo "$OUT" | grep -q "$3"; then
        echo "FAIL: $1"
        exit 1
    fi
}

run_stage "live.plan"    live_plan_check.x    "LIVE PLAN ALL PASS"
run_stage "live.proxy"   live_proxy_check.x   "LIVE PROXY ALL PASS"
run_stage "live.pattern" live_pattern_check.x "LIVE PATTERN ALL PASS"

"$APP" --nogui --nrt /tmp/live_nrt.wav --duration 10 "${MODS[@]}" \
    "$SCRIPTS/live_nrt_render.x" >/dev/null 2>&1
run_stage "live NRT render" live_nrt_compare.x "LIVE NRT PASS"

# Click regression: render a minimal redefine + stop and assert the
# waveform has no sample-to-sample discontinuities. Guards the fade
# choreography AND the engine's fade curves (the equal-power curve once ran
# backward -- see EqPowFade in engine/src/tzpl_xfader.cpp -- which turned
# every proxy crossfade into a pair of clicks).
if command -v python3 >/dev/null; then
    "$APP" --nogui --nrt /tmp/live_click.wav --duration 8 "${MODS[@]}" \
        "$SCRIPTS/live_click_probe.x" >/dev/null 2>&1
    CLICKS="$(python3 "$ROOT/integration-tests/click_scan.py" /tmp/live_click.wav)"
    echo "$CLICKS" | tail -2
    if ! echo "$CLICKS" | grep -q "CLEAN"; then
        echo "FAIL: crossfade click probe"
        exit 1
    fi
else
    echo "SKIP: click probe (no python3)"
fi

echo "PASS: live proxy system"
