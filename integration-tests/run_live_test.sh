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

echo "PASS: live proxy system"
