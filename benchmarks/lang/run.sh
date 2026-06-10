#!/usr/bin/env bash
# Runs each benchmark in Tzopilotl, Lua, LuaJIT (interpreter mode), and C++,
# taking the best of N runs. Reports wall time and confirms outputs match.
#
# LuaJIT and C++ columns appear only when their toolchains are available.
# C++ sources live in cpp/ and are compiled once (compilation is not timed).

set -e
cd "$(dirname "$0")"

TZPL="${TZPL:-../../build/lang/tzpl}"
LUA="${LUA:-lua}"
LUAJIT="${LUAJIT:-luajit}"
CXX="${CXX:-c++}"
CXXFLAGS="${CXXFLAGS:--O3 -march=native -std=c++17}"
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

# Whether an optimizing C++ compiler is available.
HAVE_CPP=0
if command -v "$CXX" >/dev/null 2>&1; then
    HAVE_CPP=1
    CPP_BIN_DIR=$(mktemp -d)
    trap 'rm -rf "$CPP_BIN_DIR"' EXIT
fi

# Format: "<bench> <tzpl_file_basename> <lua_file_basename> <cpp_file_basename>"
BENCHES=(
    "fib                  fib                  fib                  fib"
    "mandelbrot           mandelbrot           mandelbrot           mandelbrot"
    "mandelbrot_complex   mandelbrot_complex   mandelbrot           mandelbrot_complex"
    "nbody                nbody                nbody                nbody"
    "spectral_norm        spectral_norm        spectral_norm        spectral_norm"
    "binary_trees         binary_trees         binary_trees         binary_trees"
    "fannkuch_redux       fannkuch_redux       fannkuch_redux       fannkuch_redux"
    "fasta                fasta                fasta                fasta"
    "matmul               matmul               matmul               matmul"
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

compile_cpp() {
    # Compiles cpp/<base>.cpp once into CPP_BIN_DIR. Echoes the binary path on
    # success, nothing on failure (compilation is cached, failures are sticky).
    local base="$1"
    local src="cpp/${base}.cpp"
    local bin="$CPP_BIN_DIR/${base}"
    if [ -x "$bin" ]; then echo "$bin"; return 0; fi
    if [ -f "$CPP_BIN_DIR/${base}.fail" ]; then return 0; fi
    if $CXX $CXXFLAGS "$src" -o "$bin" 2>"$CPP_BIN_DIR/${base}.cclog"; then
        # Absorb the macOS first-launch security scan so timed runs are warm,
        # making C++ timing fair even at RUNS=1.
        "$bin" >/dev/null 2>&1 || true
        echo "$bin"
    else
        touch "$CPP_BIN_DIR/${base}.fail"
        echo "warning: failed to compile $src" >&2
        sed 's/^/    /' "$CPP_BIN_DIR/${base}.cclog" >&2
    fi
}

# Build the column layout. Each engine contributes a time column, and a
# <engine>/tzpl ratio column (a value < 1.00x means faster than Tzopilotl).
COLFMT=(); COLHDR=()
COLFMT+=("%-22s"); COLHDR+=("benchmark")
COLFMT+=(" %12s"); COLHDR+=("tzpl (s)")
COLFMT+=(" %12s"); COLHDR+=("lua (s)")
[ "$HAVE_LUAJIT" = "1" ] && { COLFMT+=(" %12s"); COLHDR+=("luajit (s)"); }
[ "$HAVE_CPP" = "1" ]    && { COLFMT+=(" %12s"); COLHDR+=("cpp (s)"); }
COLFMT+=(" %10s"); COLHDR+=("lua/tzpl")
[ "$HAVE_LUAJIT" = "1" ] && { COLFMT+=(" %10s"); COLHDR+=("ljit/tzpl"); }
[ "$HAVE_CPP" = "1" ]    && { COLFMT+=(" %10s"); COLHDR+=("cpp/tzpl"); }
COLFMT+=(" %8s"); COLHDR+=("match")

ROWFMT=""
for c in "${COLFMT[@]}"; do ROWFMT="$ROWFMT$c"; done
ROWFMT="$ROWFMT\n"

# Header + dashes underline.
SEP=()
for h in "${COLHDR[@]}"; do
    d=$(printf '%*s' "${#h}" ''); SEP+=("${d// /-}")
done
printf "$ROWFMT" "${COLHDR[@]}"
printf "$ROWFMT" "${SEP[@]}"

ratio() {
    # ratio <engine_time> <tzpl_time> -> "<engine/tzpl>x" or "n/a"
    awk -v b="$1" -v a="$2" 'BEGIN { if (a == 0 || b == "n/a") print "n/a"; else printf "%.2fx", b/a }'
}

for row in "${BENCHES[@]}"; do
    name=$(echo "$row" | awk '{print $1}')
    tzname=$(echo "$row" | awk '{print $2}')
    luname=$(echo "$row" | awk '{print $3}')
    cppname=$(echo "$row" | awk '{print $4}')

    resdir=$(mktemp -d)
    resfiles=()

    tz_out=$(time_one "$TZPL" "tzpl/${tzname}.x")
    tz_time=$(echo "$tz_out" | head -1)
    echo "$tz_out" | tail -n +2 > "$resdir/tzpl"; resfiles+=("$resdir/tzpl")

    lua_out=$(time_one "$LUA" "lua/${luname}.lua")
    lua_time=$(echo "$lua_out" | head -1)
    echo "$lua_out" | tail -n +2 > "$resdir/lua"; resfiles+=("$resdir/lua")

    if [ "$HAVE_LUAJIT" = "1" ]; then
        ljit_out=$(time_one "$LUAJIT" -joff "lua/${luname}.lua")
        ljit_time=$(echo "$ljit_out" | head -1)
        echo "$ljit_out" | tail -n +2 > "$resdir/ljit"; resfiles+=("$resdir/ljit")
    fi

    cpp_time="n/a"
    if [ "$HAVE_CPP" = "1" ]; then
        cpp_bin=$(compile_cpp "$cppname")
        if [ -n "$cpp_bin" ]; then
            cpp_out=$(time_one "$cpp_bin")
            cpp_time=$(echo "$cpp_out" | head -1)
            echo "$cpp_out" | tail -n +2 > "$resdir/cpp"; resfiles+=("$resdir/cpp")
        fi
    fi

    matches=$(python3 -c "
import sys
def parse(path):
    with open(path) as f:
        return [tok for tok in f.read().split() if tok]
groups = [parse(p) for p in sys.argv[1:]]
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
" "${resfiles[@]}")

    VALS=("$name" "$tz_time" "$lua_time")
    [ "$HAVE_LUAJIT" = "1" ] && VALS+=("$ljit_time")
    [ "$HAVE_CPP" = "1" ]    && VALS+=("$cpp_time")
    VALS+=("$(ratio "$lua_time" "$tz_time")")
    [ "$HAVE_LUAJIT" = "1" ] && VALS+=("$(ratio "$ljit_time" "$tz_time")")
    [ "$HAVE_CPP" = "1" ]    && VALS+=("$(ratio "$cpp_time" "$tz_time")")
    VALS+=("$matches")
    printf "$ROWFMT" "${VALS[@]}"

    rm -rf "$resdir"
done
