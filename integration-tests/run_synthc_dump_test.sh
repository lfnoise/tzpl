#!/usr/bin/env bash
# synthc analysis-dump goldens (block-engine plan M3): shapes, rates, types,
# graph cuts, trees, loops, parameter groups and delay-weighted cycles for
# every graph in integration-tests/synthc_dump/*.x, compared with the
# matching .expected file. --update rewrites the goldens.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
DIR="$ROOT/integration-tests/synthc_dump"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" -I "$ROOT/examples")
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

FAILED=0
for x in "$DIR"/*.x; do
    name="$(basename "$x" .x)"
    exp="$DIR/$name.expected"
    # Some graphs draw language random numbers while they are built (e.g.
    # effects' delay times): seed them so the dumps are stable.
    # Linux runners print realtime-setup warnings (mlockall, thread
    # priority) to stderr; they are not part of the dump.
    out="$(TZPL_LANG_SEED=0x7a91 "$APP" --nogui --no-audio "${MODS[@]}" "$x" 2>&1 \
           | grep -v -e '^mlockall failed' -e '^Failed to change thread priority')" || true
    if [ "$UPDATE" = 1 ]; then
        printf '%s\n' "$out" > "$exp"
        echo "updated $name"
        continue
    fi
    if [ ! -f "$exp" ]; then
        echo "FAIL $name: no $exp (run with --update)"
        FAILED=1
    elif ! diff -u "$exp" <(printf '%s\n' "$out") > /tmp/synthc_dump_$name.diff; then
        echo "FAIL $name (diff in /tmp/synthc_dump_$name.diff)"
        head -20 /tmp/synthc_dump_$name.diff
        FAILED=1
    else
        echo "PASS $name"
    fi
done
if [ "$FAILED" = 0 ]; then echo "SYNTHC DUMP PASS"; fi
exit "$FAILED"
