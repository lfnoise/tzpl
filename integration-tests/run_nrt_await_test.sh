#!/usr/bin/env bash
# A top-level `await delayReal` in an NRT render setup script used to deadlock
# (setup parks waiting for the render clock, which only advances after setup
# returns). The render must instead drive its clock while setup is parked, so
# the await resolves at logical render time and the render completes with
# audible output.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")
WAV=/tmp/tzpl_nrt_await.wav

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
rm -f "$WAV"

# Bound the run: if the deadlock regresses, this kills it and fails rather
# than hanging the whole suite.
"$APP" --nogui --nrt "$WAV" --duration 3 "${MODS[@]}" "$SCRIPTS/nrt_await_check.x" \
    > /tmp/tzpl_nrt_await.out 2>&1 &
pid=$!
for i in $(seq 1 30); do kill -0 $pid 2>/dev/null || break; sleep 1; done
if kill -0 $pid 2>/dev/null; then
    kill -9 $pid 2>/dev/null || true
    echo "FAIL: NRT delayReal-await render hung (deadlock regressed)"
    exit 1
fi
wait $pid 2>/dev/null || true

fail=0
grep -qF "NRT AWAIT reached end" /tmp/tzpl_nrt_await.out || { echo "FAIL: script never reached the line after the await"; fail=1; }
if command -v python3 >/dev/null; then
    peak=$(python3 - "$WAV" <<'PY'
import struct,array,sys
d=open(sys.argv[1],"rb").read(); i=d.find(b"data"); n=struct.unpack("<I",d[i+4:i+8])[0]
a=array.array("f"); a.frombytes(d[i+8:i+8+n])
print("%.4f" % (max(abs(x) for x in a) if a else 0.0))
PY
)
    echo "render peak: $peak"
    awk "BEGIN{exit !($peak > 0.1)}" || { echo "FAIL: render was silent (peak $peak)"; fail=1; }
fi
rm -f "$WAV" /tmp/tzpl_nrt_await.out
[ "$fail" -eq 0 ] || { echo "NRT AWAIT TEST FAIL"; exit 1; }
echo "NRT AWAIT TEST PASS"
