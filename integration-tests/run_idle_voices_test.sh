#!/usr/bin/env bash
# Idle voices (block engine plan M9): a released voice is skipped once it is
# done (doneWhen) or, declaring nothing, once its output has stayed below
# -120 dB for the voicer's silence time (default 0.2 s). Each synth adds 1e-7
# per voice, so its output is exactly 0 only once every voice is idle.
#  - the output becomes exactly 0 when expected (voice 1 is released at 0.5 s;
#    -1 = not before its next note at 1.5 s);
#  - every sample is within 1e-5 of a TZPL_FORMS=noidle build (no skipping),
#    including after the next note wakes the voice.
#   bash integration-tests/run_idle_voices_test.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${TZPL_BUILD_DIR:-$ROOT/build}"
APP="$BUILD/app/tzpl_app"
DRV="$BUILD/integration-tests/forms_equiv"
WORK="${IDLE_VOICES_WORK:-/tmp/tzpl_idle_voices}"
SCRIPT="$ROOT/integration-tests/scripts/idle_voices_check.x"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
[ -x "$DRV" ] || { echo "forms_equiv not built at $DRV"; exit 1; }

rm -rf "$WORK"
for forms in auto noidle; do
    out="$(SYNTHC_CORPUS_MODE=compile TZPL_FORMS=$forms TZPL_BUILD="$WORK/$forms" \
           "$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPT" 2>&1)" || true
    if grep -q "COMPILE FAIL" <<< "$out" || ! grep -q "COMPILE OK" <<< "$out"; then
        echo "$out" | tail -20
        echo "FAIL: idle voices ($forms build)"
        exit 1
    fi
done

res="$("$DRV" --idle "$WORK/auto" "$WORK/noidle")"
echo "$res"
if python3 - <<PY
import re, sys
want = {  # synth: (earliest, latest) time the output is 0 for good; -1 never
    "idle_decay": (0.74, 0.77), "idle_stereo": (0.74, 0.77), "idle_aos": (0.74, 0.77),
    "idle_done": (0.54, 0.57), "idle_long": (1.04, 1.07),
    "idle_off": (-1, -1), "idle_offdone": (-1, -1), "idle_trem": (-1, -1),
}
got = {m[0]: (float(m[1]), float(m[2]), float(m[3]))
       for m in re.findall(r'^IDLE (\S+) zero_from (\S+) max_diff (\S+) after (\S+)', """$res""", re.M)}
bad = 0
for k, (lo, hi) in want.items():
    if k not in got:
        print("FAIL", k, "missing"); bad = 1; continue
    z, d, a = got[k]
    if not (lo <= z <= hi): print("FAIL %s: output 0 from %s s, want %s..%s" % (k, z, lo, hi)); bad = 1
    if not d <= 1e-5: print("FAIL %s: differs from the noidle build by %g" % (k, d)); bad = 1
    if not a > 1e-3: print("FAIL %s: silent after the next note" % k); bad = 1
sys.exit(bad)
PY
then echo "PASS: idle voices"; else echo "FAIL: idle voices"; exit 1; fi
