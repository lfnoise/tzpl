#!/usr/bin/env bash
# Runs each benchmark in Tzopilotl, Lua, and LuaJIT (interpreter mode),
# taking the best of N runs. Reports wall time and confirms outputs match.

set -e
cd "$(dirname "$0")"

TZPL="${TZPL:-../../build/lang/tzpl}"
LUA="${LUA:-lua}"
LUAJIT="${LUAJIT:-luajit}"
RUNS="${RUNS:-3}"

if [ ! -x "$TZPL" ]; then
    echo "tzpl binary not found at $TZPL -- build the project first." >&2
    exit 1
fi

# Whether the optional `luajit` interpreter run is available.
HAVE_LUAJIT=0
if command -v "$LUAJIT" >/dev/null 2>&1; then
    HAVE_LUAJIT=1
fi

# Format: "<bench> <tzpl_file_basename> <lua_file_basename>"
BENCHES=(
    "fib                  fib                  fib"
    "mandelbrot           mandelbrot           mandelbrot"
    "mandelbrot_complex   mandelbrot_complex   mandelbrot"
    "nbody                nbody                nbody"
    "spectral_norm        spectral_norm        spectral_norm"
    "binary_trees         binary_trees         binary_trees"
    "fannkuch_redux       fannkuch_redux       fannkuch_redux"
    "fasta                fasta                fasta"
    "matmul               matmul               matmul"
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

if [ "$HAVE_LUAJIT" = "1" ]; then
    printf "%-22s %10s %10s %12s %10s %10s %10s\n" \
        "benchmark" "tzpl (s)" "lua (s)" "luajit -j-(s)" "lua/tzpl" "ljit/tzpl" "match"
    printf "%-22s %10s %10s %12s %10s %10s %10s\n" \
        "---------" "--------" "-------" "------------" "--------" "---------" "-----"
else
    printf "%-22s %12s %12s %10s %12s\n" "benchmark" "tzpl (s)" "lua (s)" "lua/tzpl" "match"
    printf "%-22s %12s %12s %10s %12s\n" "---------" "--------" "-------" "--------" "-----"
fi

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

    if [ "$HAVE_LUAJIT" = "1" ]; then
        ljit_out=$(time_one "$LUAJIT" -joff "$lua_file")
        ljit_time=$(echo "$ljit_out" | head -1)
        ljit_result=$(echo "$ljit_out" | tail -n +2)
    fi

    matches=$(python3 -c "
def parse(s):
    return [tok for tok in s.split() if tok]
a = parse('''$tz_result''')
b = parse('''$lua_result''')
${HAVE_LUAJIT:+c = parse('''$ljit_result''')}
groups = [a, b]
if '$HAVE_LUAJIT' == '1':
    groups.append(c)
n = len(groups[0])
ok = all(len(g) == n for g in groups)
if ok:
    for i in range(n):
        try:
            vals = [float(g[i]) for g in groups]
            base = sum(abs(v) for v in vals)
            if base > 0 and any(abs(v - vals[0]) / base > 1e-5 for v in vals):
                ok = False; break
        except ValueError:
            xs = [g[i] for g in groups]
            if any(x != xs[0] for x in xs):
                ok = False; break
print('yes' if ok else 'NO')
")

    ratio_lua=$(awk -v a="$tz_time" -v b="$lua_time" 'BEGIN { if (a == 0) print "inf"; else printf "%.2fx", b/a }')
    if [ "$HAVE_LUAJIT" = "1" ]; then
        ratio_ljit=$(awk -v a="$tz_time" -v b="$ljit_time" 'BEGIN { if (a == 0) print "inf"; else printf "%.2fx", b/a }')
        printf "%-22s %10s %10s %12s %10s %10s %10s\n" \
            "$name" "$tz_time" "$lua_time" "$ljit_time" "$ratio_lua" "$ratio_ljit" "$matches"
    else
        printf "%-22s %12s %12s %10s %12s\n" "$name" "$tz_time" "$lua_time" "$ratio_lua" "$matches"
    fi
done
