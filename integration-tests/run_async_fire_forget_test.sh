#!/usr/bin/env bash
# A fire-and-forget async fn (its result Future discarded, no top-level await
# anywhere) must still run its continuation after the await: resolving a
# future only enqueues its waiters, so with nobody parked to drain the queue
# the resolver has to. Regression for a silent live-coding session where a
# proxy's background compile finished but its source-swap continuation never
# ran (define/play evaluated without an await).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="${TZPL_BUILD_DIR:-$ROOT/build}/app/tzpl_app"
SCRIPTS="$ROOT/integration-tests/scripts"
MODS=(-I "$ROOT/lang/modules" -I "$ROOT/bridge/modules")

[ -x "$APP" ] || { echo "tzpl_app not built at $APP"; exit 1; }

DIR=/tmp/tzpl_test_async_ff
rm -rf "$DIR"
"$APP" --nogui "${MODS[@]}" "$SCRIPTS/async_fire_forget_check.x" >/dev/null 2>&1

fail=0
for tag in a b; do
    if [ -f "$DIR/$tag.out" ] && grep -qF "FIREFORGET $tag ran: payload" "$DIR/$tag.out"; then
        echo "PASS fire-and-forget continuation ($tag) ran"
    else
        echo "FAIL fire-and-forget continuation ($tag) never ran"
        fail=1
    fi
done
rm -rf "$DIR"
[ "$fail" -eq 0 ] || { echo "ASYNC FIRE-FORGET TEST FAIL"; exit 1; }
echo "ASYNC FIRE-FORGET TEST PASS"
