#!/usr/bin/env bash
# Equivalence of forms (block engine plan M8): every dump-corpus synth
# compiled with the forms the compiler chooses agrees with the same synth
# compiled with TZPL_FORMS=serial (every loop plain and serial), run in whole
# blocks, one sample at a time and odd chunks (integration-tests/src/
# forms_equiv.cpp).
#   bash integration-tests/run_forms_equivalence_test.sh [name-substring]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${TZPL_BUILD_DIR:-$ROOT/build}"
APP="$BUILD/app/tzpl_app"
DRV="$BUILD/integration-tests/forms_equiv"
WORK="${FORMS_WORK:-/tmp/tzpl_forms_equiv}"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules" -I "$ROOT/examples")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }
[ -x "$DRV" ] || { echo "forms_equiv not built at $DRV"; exit 1; }
rm -rf "$WORK"
for forms in auto serial; do
    for x in "$ROOT"/integration-tests/synthc_dump/*.x; do
        out="$(SYNTHC_CORPUS_MODE=compile TZPL_FORMS=$forms TZPL_LANG_SEED=0x7a91 \
               TZPL_BUILD="$WORK/$forms" "$APP" --nogui --no-audio "${MODS[@]}" "$x" 2>&1)" || true
        if echo "$out" | grep -q "COMPILE FAIL"; then
            echo "$out" | grep "COMPILE FAIL"
            exit 1
        fi
    done
done
# Seeded random streams (tzpl_random.hpp), so both builds draw the same numbers.
TZPL_RNG_SEED=0x5eed "$DRV" "$WORK/auto" "$WORK/serial" ${1:-}
