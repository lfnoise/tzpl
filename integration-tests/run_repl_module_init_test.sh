#!/usr/bin/env bash
# Module init after a failed REPL eval: a cell that imports a module and then
# fails its own typecheck installs the module's code and globals but (before
# the fix) never ran its init block -- every module-level `let` stayed null
# for the rest of the session, and the next cell that touched the module read
# garbage and segfaulted (the music_fx_demos GUI crash of 2026-08-14).
# REPLSession now runs pending module inits even when the eval fails.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

out=$(TZPL_EVAL_CELLS=1 "$APP" --nogui \
        -I "$SCRIPTS" -I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" \
        < "$SCRIPTS/repl_module_init_cells.txt" 2>&1) || {
    echo "FAIL: cell runner crashed/exited nonzero"; echo "$out"; exit 1
}

# The first cell must still FAIL (that's the premise of the regression).
echo "$out" | grep -q "requires numeric operands" || {
    echo "FAIL: cell 1 no longer fails -- test premise lost"; echo "$out"; exit 1
}
echo "$out" | grep -q "UNREACHABLE cell1" && {
    echo "FAIL: cell 1 body ran despite its type error"; echo "$out"; exit 1
}

# The second cell must read the module's initialized state.
echo "$out" | grep -qx "3" || {
    echo "FAIL: readTable() did not return 3 (module init never ran)"
    echo "$out"; exit 1
}
echo "$out" | grep -q "MODULE INIT AFTER FAILED EVAL: OK" || {
    echo "FAIL: second cell did not complete"; echo "$out"; exit 1
}

echo "REPL MODULE INIT TEST PASS"
