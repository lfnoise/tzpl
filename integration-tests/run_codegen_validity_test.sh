#!/usr/bin/env bash
# Codegen validity regressions: shapes where both synthdef compilers once
# emitted the SAME invalid C++ (byte-diff suites passed, clang refused).
# Checks byte parity AND a real clang compile + load through both compilers.
# Cases: SIMD mixed-width binop (f32 const / f64 signal from the rewriter's
# reciprocal), and put/join with scalar-constant inputs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/build/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/synthc_codegen_validity.x" 2>/dev/null | grep -E 'PASS|FAIL')"
echo "$OUT"
if echo "$OUT" | grep -q "CODEGEN VALIDITY PASS"; then
    echo "PASS: codegen validity"
    exit 0
fi
echo "FAIL: codegen validity"
exit 1
