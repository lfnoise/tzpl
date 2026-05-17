#!/usr/bin/env bash
# Runs each benchmark in Tzopilotl and Lua, taking the best of N runs.
# Reports wall time and confirms outputs match.

set -e
cd "$(dirname "$0")"

TZPL="${TZPL:-../../build/lang/tzpl}"
LUA="${LUA:-lua}"
RUNS="${RUNS:-3}"

if [ ! -x "$TZPL" ]; then
    echo "tzpl binary not found at $TZPL -- build the project first." >&2
    exit 1
fi

# Format: "<bench> <tzpl_file_basename> <lua_file_basename>"
BENCHES=(
    "fib                  fib                  fib"
    "mandelbrot           mandelbrot           mandelbrot"
    "mandelbrot_complex   mandelbrot_complex   mandelbrot"
    "nbody                nbody                nbody"
    "spectral_norm        spectral_norm        spectral_norm"
)

time_one() {
    # Prints best wall-clock seconds over RUNS executions to stdout (first line),
    # then the stdout of the last successful run.
    local best=""
    local last_out=""
    for _ in $(seq 1 "$RUNS"); do
        local t
        { /usr/bin/time -p "$@" >/tmp/bench_stdout; } 2>/tmp/bench_time
        t=$(awk '/^real/ {print $2}' /tmp/bench_time)
        last_out=$(cat /tmp/bench_stdout)
        if [ -z "$best" ] || awk -v a="$t" -v b="$best" 'BEGIN { exit !(a < b) }'; then
            best="$t"
        fi
    done
    echo "$best"
    echo "$last_out"
}

printf "%-22s %12s %12s %10s %12s\n" "benchmark" "tzpl (s)" "lua (s)" "tzpl/lua" "match"
printf "%-22s %12s %12s %10s %12s\n" "---------" "--------" "-------" "--------" "-----"
for row in "${BENCHES[@]}"; do
    name=$(echo "$row" | awk '{print $1}')
    tzname=$(echo "$row" | awk '{print $2}')
    luname=$(echo "$row" | awk '{print $3}')
    tz_file="tzpl/${tzname}.x"
    lua_file="lua/${luname}.lua"

    tz_out=$(time_one "$TZPL" "$tz_file")
    tz_time=$(echo "$tz_out" | head -1)
    tz_result=$(echo "$tz_out" | tail -n +2)

    lua_out=$(time_one "$LUA" "$lua_file")
    lua_time=$(echo "$lua_out" | head -1)
    lua_result=$(echo "$lua_out" | tail -n +2)

    matches=$(python3 -c "
def parse(s):
    return [tok for tok in s.split() if tok]
a = parse('''$tz_result''')
b = parse('''$lua_result''')
if len(a) != len(b):
    print('NO'); raise SystemExit
ok = True
for x, y in zip(a, b):
    try:
        fx, fy = float(x), float(y)
        if abs(fx) + abs(fy) > 0 and abs(fx - fy) / (abs(fx) + abs(fy)) > 1e-5:
            ok = False; break
    except ValueError:
        if x != y:
            ok = False; break
print('yes' if ok else 'NO')
")

    ratio=$(awk -v a="$tz_time" -v b="$lua_time" 'BEGIN { if (b == 0) print "inf"; else printf "%.2fx", a/b }')
    printf "%-22s %12s %12s %10s %12s\n" "$name" "$tz_time" "$lua_time" "$ratio" "$matches"
done
