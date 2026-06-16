#!/usr/bin/env bash
# Tier-3 differential test for the Tzopilotl-hosted synthdef compiler (synthc).
#
# Renders the SAME synth twice -- once compiled by the C++ compiler (defSynth)
# and once by synthc (defSynthX) -- to WAV via the app's --nrt mode, then asserts
# the audio is bit-identical. Covers an audio-rate synth and a control-bearing
# synth driven by scheduled control changes (which exercises the engine's
# control-event path: iso-group activation, processEvents, instance vars).
# synthc's generated C++ byte-matches the C++ generator, so the rendered audio
# must match exactly; the comparator also asserts each render is non-silent.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
DUR=0.1

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

render() {  # <out.wav> <script>
    "$APP" --nogui --nrt "$1" --duration "$DUR" "${MODS[@]}" "$2" >/dev/null 2>&1
}

render /tmp/synthc_ab_ref.wav         "$SCRIPTS/synthc_render_ab_ref.x"
render /tmp/synthc_ab_synthc.wav      "$SCRIPTS/synthc_render_ab_synthc.x"
render /tmp/synthc_ab_ctrl_ref.wav    "$SCRIPTS/synthc_render_ab_ctrl_ref.x"
render /tmp/synthc_ab_ctrl_synthc.wav "$SCRIPTS/synthc_render_ab_ctrl_synthc.x"
render /tmp/synthc_ab_vdelay_ref.wav    "$SCRIPTS/synthc_render_ab_vdelay_ref.x"
render /tmp/synthc_ab_vdelay_synthc.wav "$SCRIPTS/synthc_render_ab_vdelay_synthc.x"

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/synthc_render_ab_compare.x" 2>/dev/null)"
echo "$OUT"
if echo "$OUT" | grep -q "RENDER AB PASS"; then
    echo "PASS: synthc render A/B is bit-identical to the C++ compiler (audio + control)"
    exit 0
fi
echo "FAIL: synthc render A/B differs"
exit 1
