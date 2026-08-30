#!/usr/bin/env bash
# Voicer allocator invariants (shared/tzpl_voicer.hpp): noteID ownership
# transfers at noteOn, so duplicate noteOns release the previous note instead
# of orphaning it, released notes stay addressable by ID through their decay
# until replaced, and repeated noteOffs cannot corrupt activeVoices_. Pure
# bookkeeping over the header -- no audio runs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/integration-tests/test_voicer"

[ -x "$BIN" ] || { echo "test_voicer not built at $BIN"; exit 1; }

out=$("$BIN" 2>&1)
if echo "$out" | grep -qF "VOICER TEST PASS"; then
    echo "VOICER ALLOC TEST PASS"
else
    echo "$out" | grep -E "PASS|FAIL" || true
    echo "VOICER ALLOC TEST FAIL"
    exit 1
fi
