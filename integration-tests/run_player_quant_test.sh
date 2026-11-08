#!/usr/bin/env bash
# music.play quantized play/replace: render player_quant_render.x offline and
# check that every stream starts on its grid beat and stops at its switch
# (player_quant_check.py, pure-stdlib Python).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
WAV=/tmp/tzpl_player_quant.wav

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
command -v python3 >/dev/null || { echo "SKIP: player quant test (no python3)"; exit 0; }
rm -f "$WAV"

"$APP" --nogui --nrt "$WAV" --duration 12 "${MODS[@]}" \
    "$SCRIPTS/player_quant_render.x" >/dev/null 2>&1
[ -f "$WAV" ] || { echo "FAIL: render produced no file"; exit 1; }

if python3 -I "$SCRIPTS/player_quant_check.py" "$WAV"; then
    rm -f "$WAV"
    echo "PLAYER QUANT TEST PASS"
else
    echo "PLAYER QUANT TEST FAIL (render kept at $WAV)"
    exit 1
fi
