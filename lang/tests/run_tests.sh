#!/usr/bin/env bash
#
# Test runner for the Tzopilotl interpreter.
# Finds *.x test files under tests/, runs them, and compares output against golden files.
#
set -euo pipefail

# --- Configuration ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TZPL="${TZPL_BIN:-$(cd "$ROOT_DIR/.." && pwd)/build/lang/tzpl}"
TIMEOUT_SEC=10

# --- Colors ---
if [[ -t 1 ]]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[0;33m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    RESET='\033[0m'
else
    RED='' GREEN='' YELLOW='' CYAN='' BOLD='' RESET=''
fi

# --- Flags ---
UPDATE=false
FILTER=""
VERBOSE=false
STOP_ON_FAIL=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --update)   UPDATE=true; shift ;;
        --filter)   FILTER="$2"; shift 2 ;;
        --verbose)  VERBOSE=true; shift ;;
        --stop-on-fail) STOP_ON_FAIL=true; shift ;;
        -h|--help)
            echo "Usage: $0 [--update] [--filter <pattern>] [--verbose] [--stop-on-fail]"
            echo ""
            echo "  --update        Regenerate .expected / .expected_err golden files"
            echo "  --filter PAT    Only run tests whose path contains PAT"
            echo "  --verbose       Show diffs on failure"
            echo "  --stop-on-fail  Stop at first failure"
            exit 0
            ;;
        *) echo "Unknown flag: $1"; exit 1 ;;
    esac
done

# --- Sanity checks ---
if [[ ! -x "$TZPL" ]]; then
    echo -e "${RED}Error: interpreter not found at $TZPL${RESET}"
    echo "Run 'cmake --build build' first."
    exit 1
fi

# --- Collect test files ---
# Skip _-prefixed directories (helper modules) and _-prefixed files.
TEST_FILES=()
while IFS= read -r f; do
    TEST_FILES+=("$f")
done < <(find "$SCRIPT_DIR" -name '*.x' -not -path '*/_*' | sort)

# --- Counters ---
PASS=0
FAIL=0
SKIP=0
ERROR=0
TOTAL=0
START_TIME=$SECONDS

for test_file in "${TEST_FILES[@]}"; do
    # Apply filter
    if [[ -n "$FILTER" && "$test_file" != *"$FILTER"* ]]; then
        continue
    fi

    TOTAL=$((TOTAL + 1))
    rel_path="${test_file#$SCRIPT_DIR/}"

    # Check for skip marker: first line starts with "-- SKIP:"
    first_line=$(head -1 "$test_file")
    if [[ "$first_line" == "-- SKIP:"* ]]; then
        reason="${first_line#-- SKIP:}"
        echo -e "${YELLOW}SKIP${RESET} $rel_path —${reason}"
        SKIP=$((SKIP + 1))
        continue
    fi

    # Determine test directory (for -I flag) and expected file
    test_dir="$(dirname "$test_file")"
    base="${test_file%.x}"
    is_error_test=false

    if [[ "$test_dir" == *"/errors" ]]; then
        is_error_test=true
        expected_file="${base}.expected_err"
    else
        expected_file="${base}.expected"
    fi

    # Platform-specific golden override: on Linux a .expected.linux (or
    # .expected_err.linux) file wins when present. Used for the handful of
    # tests whose output differs because macOS-only libm entry points
    # (__sinpi, __exp10, ...) fall back to portable formulas elsewhere.
    if [[ "$(uname)" == "Linux" && -f "${expected_file}.linux" ]]; then
        expected_file="${expected_file}.linux"
    fi

    # Build -I flags: always include test_dir, plus a _lib subdirectory if it
    # exists, plus the real lang/modules so tests resolve shared modules (message,
    # message, strings, ...) from the actual files users get -- not a copy. A
    # test-local _lib still takes precedence (listed first) for module-system
    # fixtures that intentionally shadow.
    I_FLAGS=(-I "$test_dir")
    if [[ -d "$test_dir/_lib" ]]; then
        I_FLAGS+=(-I "$test_dir/_lib")
    fi
    I_FLAGS+=(-I "$ROOT_DIR/modules")

    # Check for @rt marker: "-- @rt" means pass --rt flag
    if grep -q '^-- @rt' "$test_file"; then
        I_FLAGS+=(--rt)
    fi

    # Check for @fails marker: "-- @fails" means the script is EXPECTED to
    # exit nonzero (a runtime error test: panic, unwrap on none, ...).
    # stdout is still golden-compared against .expected.
    expects_fail=false
    if grep -q '^-- @fails' "$test_file"; then
        expects_fail=true
    fi

    # Run the test with timeout
    stdout_file=$(mktemp)
    stderr_file=$(mktemp)
    exit_code=0

    if command -v gtimeout &>/dev/null; then
        TIMEOUT_CMD="gtimeout"
    elif command -v timeout &>/dev/null; then
        TIMEOUT_CMD="timeout"
    else
        TIMEOUT_CMD=""
    fi

    if [[ -n "$TIMEOUT_CMD" ]]; then
        $TIMEOUT_CMD "${TIMEOUT_SEC}s" "$TZPL" "${I_FLAGS[@]}" "$test_file" \
            >"$stdout_file" 2>"$stderr_file" || exit_code=$?
    else
        # No timeout command available; run directly
        "$TZPL" "${I_FLAGS[@]}" "$test_file" \
            >"$stdout_file" 2>"$stderr_file" || exit_code=$?
    fi

    timed_out=false
    if [[ $exit_code -eq 124 ]]; then
        timed_out=true
    fi

    # For error tests, strip the absolute path prefix from stderr so that
    # .expected_err files are portable across machines / checkout locations.
    if $is_error_test; then
        sed -i.bak "s|$ROOT_DIR/||g" "$stderr_file"
        rm -f "${stderr_file}.bak"
    fi

    # --- Update mode ---
    if $UPDATE; then
        if $is_error_test; then
            actual_file="$stderr_file"
        else
            actual_file="$stdout_file"
        fi
        if [[ "$(uname)" == "Linux" && "$expected_file" != *.linux ]]; then
            # Never overwrite the shared golden file from Linux -- it is the
            # macOS-authored source of truth. Write a .linux override, and
            # only when the output actually differs.
            if [[ -f "$expected_file" ]] && diff -q "$expected_file" "$actual_file" >/dev/null 2>&1; then
                echo -e "${CYAN}UNCHANGED${RESET} $rel_path"
            else
                cp "$actual_file" "${expected_file}.linux"
                echo -e "${CYAN}UPDATED${RESET} $rel_path (.linux override)"
            fi
        else
            cp "$actual_file" "$expected_file"
            echo -e "${CYAN}UPDATED${RESET} $rel_path"
        fi
        rm -f "$stdout_file" "$stderr_file"
        continue
    fi

    # --- Validation ---
    if $timed_out; then
        echo -e "${RED}TIMEOUT${RESET} $rel_path (>${TIMEOUT_SEC}s)"
        FAIL=$((FAIL + 1))
        rm -f "$stdout_file" "$stderr_file"
        if $STOP_ON_FAIL; then break; fi
        continue
    fi

    if [[ ! -f "$expected_file" ]]; then
        echo -e "${YELLOW}NO EXPECTED${RESET} $rel_path (run with --update to generate)"
        SKIP=$((SKIP + 1))
        rm -f "$stdout_file" "$stderr_file"
        continue
    fi

    if ! $is_error_test && ! $expects_fail && [[ $exit_code -ne 0 ]]; then
        echo -e "${RED}FAIL${RESET} $rel_path (exit code $exit_code)"
        FAIL=$((FAIL + 1))
        if $VERBOSE; then
            echo -e "${BOLD}--- stderr ---${RESET}"
            cat "$stderr_file"
            echo ""
        fi
        rm -f "$stdout_file" "$stderr_file"
        if $STOP_ON_FAIL; then break; fi
        continue
    fi

    if $expects_fail && [[ $exit_code -eq 0 ]]; then
        echo -e "${RED}FAIL${RESET} $rel_path (expected nonzero exit, got 0)"
        FAIL=$((FAIL + 1))
        rm -f "$stdout_file" "$stderr_file"
        if $STOP_ON_FAIL; then break; fi
        continue
    fi

    if ! $is_error_test && [[ -s "$stderr_file" ]]; then
        echo -e "${RED}FAIL${RESET} $rel_path (unexpected stderr)"
        FAIL=$((FAIL + 1))
        if $VERBOSE; then
            echo -e "${BOLD}--- stderr ---${RESET}"
            cat "$stderr_file"
            echo ""
        fi
        rm -f "$stdout_file" "$stderr_file"
        if $STOP_ON_FAIL; then break; fi
        continue
    fi

    # Compare
    if $is_error_test; then
        actual_file="$stderr_file"
    else
        actual_file="$stdout_file"
    fi

    if diff -q "$expected_file" "$actual_file" >/dev/null 2>&1; then
        echo -e "${GREEN}PASS${RESET} $rel_path"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${RESET} $rel_path"
        FAIL=$((FAIL + 1))
        if $VERBOSE; then
            echo -e "${BOLD}--- Expected vs Actual ---${RESET}"
            diff --color=auto -u "$expected_file" "$actual_file" || true
            echo ""
        fi
        if $STOP_ON_FAIL; then
            rm -f "$stdout_file" "$stderr_file"
            break
        fi
    fi

    rm -f "$stdout_file" "$stderr_file"
done

ELAPSED=$((SECONDS - START_TIME))

echo ""
echo -e "${BOLD}========================================${RESET}"
echo -e "${BOLD}Results:${RESET} ${GREEN}${PASS} passed${RESET}, ${RED}${FAIL} failed${RESET}, ${YELLOW}${SKIP} skipped${RESET} / ${TOTAL} total"
echo -e "${BOLD}Time:${RESET} ${ELAPSED}s"
echo -e "${BOLD}========================================${RESET}"

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
