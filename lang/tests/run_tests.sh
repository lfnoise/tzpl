#!/usr/bin/env bash
#
# Test runner for the Language X interpreter.
# Finds *.x test files under tests/, runs them, and compares output against golden files.
#
set -euo pipefail

# --- Configuration ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LANGX="$(cd "$ROOT_DIR/.." && pwd)/build/lang/langx"
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
if [[ ! -x "$LANGX" ]]; then
    echo -e "${RED}Error: interpreter not found at $LANGX${RESET}"
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

    # Build -I flags: always include test_dir, plus _lib subdirectory if it exists
    I_FLAGS=(-I "$test_dir")
    if [[ -d "$test_dir/_lib" ]]; then
        I_FLAGS+=(-I "$test_dir/_lib")
    fi

    # Check for @rt marker: "-- @rt" means pass --rt flag
    if grep -q '^-- @rt' "$test_file"; then
        I_FLAGS+=(--rt)
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
        $TIMEOUT_CMD "${TIMEOUT_SEC}s" "$LANGX" "${I_FLAGS[@]}" "$test_file" \
            >"$stdout_file" 2>"$stderr_file" || exit_code=$?
    else
        # No timeout command available; run directly
        "$LANGX" "${I_FLAGS[@]}" "$test_file" \
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
            cp "$stderr_file" "$expected_file"
        else
            cp "$stdout_file" "$expected_file"
        fi
        echo -e "${CYAN}UPDATED${RESET} $rel_path"
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
