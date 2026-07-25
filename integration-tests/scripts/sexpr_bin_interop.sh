#!/usr/bin/env bash
# Proves the C++ encoder (shared/tzpl_sexpr_bin.hpp) and the Tzopilotl encoder
# (lang/modules/std/messageEncoding.x) agree byte-for-byte on the TZB wire
# format (documented in lang/docs/FFI_Guide.html section 15.5): both
# encode the same Msg value and their byte dumps are diffed.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TZPL="$ROOT/build/lang/tzpl"
SELFTEST_SRC="$ROOT/tools/sexpr_bin_selftest.cpp"
SELFTEST_BIN="$(mktemp)"
LANG_DUMP="$(mktemp).x"
CPP_OUT="$(mktemp)"
LANG_OUT="$(mktemp)"
cleanup() { rm -f "$SELFTEST_BIN" "$LANG_DUMP" "$CPP_OUT" "$LANG_OUT"; }
trap cleanup EXIT

if [[ ! -x "$TZPL" ]]; then echo "SKIP: $TZPL not built"; exit 0; fi

# Build + run the C++ self-test; keep just its byte-dump line.
c++ -std=c++23 -I "$ROOT/shared" "$SELFTEST_SRC" -o "$SELFTEST_BIN" || { echo "FAIL: C++ self-test did not compile"; exit 1; }
"$SELFTEST_BIN" | head -1 > "$CPP_OUT"

# Encode the same value in Tzopilotl and dump its bytes.
cat > "$LANG_DUMP" <<'EOF'
import std.message.*;
import std.messageEncoding.*;
fn dump(b Bytes) Void {
    var i = 0; let n = b byteLength;
    while (i < n) { print((b u8At(i)) toString); print(" "); i = i + 1; }
    println("");
}
dump(encode(Msg.vec([
    Msg.symbol(toSymbol("note")),
    Msg.int(60),
    Msg.float(0.5),
    Msg.string("hi"),
    Msg.vec([Msg.int(1), Msg.bool(true)]),
])));
EOF
"$TZPL" -I "$ROOT/lang/modules" "$LANG_DUMP" > "$LANG_OUT"

if diff -q "$CPP_OUT" "$LANG_OUT" >/dev/null; then
    echo "INTEROP: PASS (C++ and message.x are byte-identical)"
    exit 0
else
    echo "INTEROP: FAIL"
    echo "--- C++  ---"; cat "$CPP_OUT"
    echo "--- lang ---"; cat "$LANG_OUT"
    exit 1
fi
