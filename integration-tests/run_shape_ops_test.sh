#!/usr/bin/env bash
# Tensor shape operations along an axis (block engine plan 4.8, M8b):
#  - every case's result shape (analysis dump OUT line) is the reference's;
#  - every case's plugin outputs (result - reference) = 0 in whole blocks and
#    odd chunks (forms_equiv --zero), in the forms the compiler chooses and
#    with TZPL_FORMS=serial;
#  - out-of-range axes and mismatched shapes are compile errors.
#   bash integration-tests/run_shape_ops_test.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${TZPL_BUILD_DIR:-$ROOT/build}"
APP="$BUILD/app/tzpl_app"
DRV="$BUILD/integration-tests/forms_equiv"
WORK="${SHAPE_OPS_WORK:-/tmp/tzpl_shape_ops}"
SCRIPT="$ROOT/integration-tests/scripts/shape_ops_check.x"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
[ -x "$DRV" ] || { echo "forms_equiv not built at $DRV"; exit 1; }
FAILED=0

# Shapes: each EXPECT line against its case's OUT line, and no errors.
"$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPT" > "$WORK.dump" 2>&1 || true
if python3 - "$WORK.dump" <<'PY'
import re, sys
t = open(sys.argv[1]).read()
exp = dict(re.findall(r'^EXPECT (\S+) (\S+)$', t, re.M))
bad = []
for m in re.finditer(r'^SYNTH (\S+)\n(.*?)(?=^SYNTH |\Z)', t, re.M | re.S):
    name = m.group(1)
    o = re.search(r'^OUT 0 out \S+ (\S+)', m.group(2), re.M)
    errs = re.findall(r'^ERROR (.*)$', m.group(2), re.M)
    if name in exp and ((o.group(1) if o else None) != exp[name] or errs):
        bad.append((name, exp[name], o.group(1) if o else None, errs))
seen = set(re.findall(r'^SYNTH (\S+)$', t, re.M))
bad += [(k, exp[k], "missing", []) for k in exp if k not in seen]
for b in bad: print("FAIL shape", *b)
print("%d cases, %d shape failures" % (len(exp), len(bad)))
sys.exit(1 if bad or not exp else 0)
PY
then echo "PASS shapes"; else FAILED=1; fi

# Errors the compiler must report.
ERR="$(SHAPE_OPS_MODE=errors "$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPT" 2>&1 || true)"
for want in "axis 2 is outside a rank-2 signal" "axis -3 is outside a rank-2 signal" \
            "drop leaves no elements" "join: shapes 3x4 and 2x3 differ" \
            "stack: shapes 3x4 and 6 differ" "reshape: 12 channels do not divide by 5" \
            "transpose: [0, 0, 1] is not a permutation"; do
    if grep -qF "ERROR $want" <<< "$ERR"; then echo "PASS error: $want"
    else echo "FAIL error not reported: $want"; FAILED=1; fi
done

# Values: (result - reference) is 0 in both builds.
rm -rf "$WORK"
for forms in auto serial; do
    out="$(SHAPE_OPS_MODE=compile SYNTHC_CORPUS_MODE=compile TZPL_FORMS=$forms \
           TZPL_BUILD="$WORK/$forms" "$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPT" 2>&1)" || true
    if grep -q "COMPILE FAIL" <<< "$out"; then
        echo "$out" | grep "COMPILE FAIL" | head -20
        FAILED=1
        continue
    fi
    echo "--- $forms"
    "$DRV" --zero "$WORK/$forms" || FAILED=1
done

if [ "$FAILED" -ne 0 ]; then echo "FAIL: shape operations"; exit 1; fi
echo "PASS: shape operations"
