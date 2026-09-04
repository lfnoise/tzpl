#!/usr/bin/env bash
# rtOnly enforcement test: silo-only FFI functions (playNote, releaseNote,
# scheduleTask, siloOutbox) are compile errors on the NRT VM -- directly and
# through .x wrappers (spawn) via body-check taint -- while silo targets and
# NRT imports of the wrapper-carrying modules keep working.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

# Negative half: must FAIL to compile, with the silo-only diagnostic, and
# never reach the script body.
out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/rtonly_nrt_err.x" 2>&1) && {
    echo "FAIL: rtonly_nrt_err.x compiled/ran but should have been rejected"
    exit 1
}
echo "$out" | grep -q "silo-only" || {
    echo "FAIL: expected 'silo-only' diagnostic, got:"; echo "$out"; exit 1
}
echo "$out" | grep -q "UNREACHABLE" && {
    echo "FAIL: script body ran despite rtOnly violation"; exit 1
}
echo "PASS rtonly_nrt_err.x (rejected with silo-only diagnostic)"

# Positive half: silo load + NRT wrapper imports + NRT actor spawn.
out=$("$APP" --nogui "${MODS[@]}" "$SCRIPTS/rtonly_ok.x" 2>&1)
echo "$out" | grep -q "RTONLY silo load: OK" || {
    echo "FAIL: silo load of spawn/playNote task code"; echo "$out"; exit 1
}
echo "$out" | grep -q "RTONLY actor spawn: OK" || {
    echo "FAIL: NRT actor spawn overload"; echo "$out"; exit 1
}
echo "PASS rtonly_ok.x (silo load + NRT imports + actor spawn)"
echo "RTONLY TEST PASS"
