#!/usr/bin/env bash
# Compile every effects + instruments library def to a real dylib.
#
# The differential suites prove the two synthdef compilers byte-match; this
# proves the matched output is actually valid C++ (clang compiles, links,
# and the def registers). Catches parity-preserving codegen bugs like the
# fxSympathetic cross-iso-group local (both compilers emitted the same
# invalid code, so the diff suites passed while the def was uncompilable).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

OUT="$("$APP" --nogui --no-audio "${MODS[@]}" "$SCRIPTS/fx_compile_all.x" 2>/dev/null | grep -E '^(PASS|FAIL|FX COMPILE)')"
echo "$OUT"
if echo "$OUT" | grep -q "FX COMPILE ALL PASS"; then
    echo "PASS: all library defs compile to dylibs"
    exit 0
fi
echo "FAIL: library def compile"
exit 1
