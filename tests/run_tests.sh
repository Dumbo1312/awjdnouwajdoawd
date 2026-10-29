#!/usr/bin/env bash
# Regression tests for the modified UEFIPatch.
#
# Each case either exercises a feature added here or pins down a bug that
# upstream UEFITool 0.28.0 still has. Run it after any change to
# UEFITool-0.28.0/UEFIPatch or to the patch helpers in ffsengine.cpp.
#
# Usage: tests/run_tests.sh [path/to/UEFIPatch]
#        (with no argument the script builds UEFIPatch itself)

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PATCH_DB="$REPO_ROOT/patches/performance.txt"
PASS=0
FAIL=0

# ---------------------------------------------------------------- helpers ---

fail() {
    printf 'FAIL  %s\n' "$1"
    shift
    for line in "$@"; do printf '        %s\n' "$line"; done
    FAIL=$((FAIL + 1))
}

pass() {
    printf 'ok    %s\n' "$1"
    PASS=$((PASS + 1))
}

# run <expected_exit> <name> -- <uefipatch args...>
# Stores combined output in $OUT for the caller to inspect further.
run() {
    local expected="$1" name="$2"; shift 3
    OUT="$("$UEFIPATCH" "$@" 2>&1)"
    local rc=$?
    if [ "$rc" != "$expected" ]; then
        fail "$name" "expected exit $expected, got $rc" "$OUT"
        return 1
    fi
    return 0
}

expect_match() {
    local name="$1" pattern="$2"
    if printf '%s' "$OUT" | grep -qE -- "$pattern"; then
        return 0
    fi
    fail "$name" "output did not match: $pattern" "$OUT"
    return 1
}

expect_no_match() {
    local name="$1" pattern="$2"
    if printf '%s' "$OUT" | grep -qE -- "$pattern"; then
        fail "$name" "output should not have matched: $pattern" "$OUT"
        return 1
    fi
    return 0
}

check() {
    local name="$1"; shift
    if "$@"; then pass "$name"; fi
}

# ------------------------------------------------------------------ build ---

if [ $# -ge 1 ]; then
    UEFIPATCH="$1"
else
    QMAKE=""
    for candidate in qmake /usr/lib/qt5/bin/qmake qmake-qt5 qmake6; do
        if command -v "$candidate" > /dev/null 2>&1; then QMAKE="$candidate"; break; fi
        if [ -x "$candidate" ]; then QMAKE="$candidate"; break; fi
    done
    if [ -z "$QMAKE" ]; then
        echo "no qmake found; install Qt or pass the UEFIPatch binary as an argument" >&2
        exit 2
    fi
    echo "Building UEFIPatch with $QMAKE ..."
    mkdir -p "$WORK/build"
    ( cd "$WORK/build" \
        && "$QMAKE" "$REPO_ROOT/UEFITool-0.28.0/UEFIPatch/uefipatch.pro" > /dev/null \
        && make -j"$(nproc 2>/dev/null || echo 2)" > "$WORK/build.log" 2>&1 ) \
        || { echo "build failed, see $WORK/build.log" >&2; cat "$WORK/build.log" >&2; exit 2; }
    UEFIPATCH="$WORK/build/UEFIPatch"
fi

echo "Testing $UEFIPATCH"
"$UEFIPATCH" --help | head -1
echo

IMAGE="$WORK/test.bin"
python3 "$REPO_ROOT/tests/make_test_image.py" "$IMAGE" > /dev/null

HASWELL="F7731B4C-58A2-4DF4-8980-5645D39ECE58"
SIINIT="299D6F8B-2EC9-4E40-9EC6-DDAA7EBF5FD9"
HASWELL_FIND="75080FBAE80F89442430"
HASWELL_REPL="EB080FBAE80F89442430"

# ------------------------------------------------------- crash regressions ---
# Upstream reads patchList.at(1)/at(2) without checking how many fields the
# ":" split produced, so a truncated patch spec segfaults (exit 139).

printf '%s 10 P:%s\n' "$HASWELL" "$HASWELL_FIND" > "$WORK/truncated.txt"
check "truncated patch spec is rejected, not a crash" \
    run 1 "truncated patch spec is rejected, not a crash" -- \
        "$IMAGE" "$WORK/truncated.txt" -o "$WORK/out.bin"
check "truncated patch spec names the offending line" \
    expect_match "truncated patch spec names the offending line" 'line 1.*Type:Find:Replace'

# Upstream turns an unparseable GUID into the null GUID and then silently
# reports that nothing could be patched.
printf 'not-a-guid 10 P:%s:%s\n' "$HASWELL_FIND" "$HASWELL_REPL" > "$WORK/badguid.txt"
check "unparseable GUID is an error" \
    run 1 "unparseable GUID is an error" -- \
        "$IMAGE" "$WORK/badguid.txt" -o "$WORK/out.bin"
check "unparseable GUID is named in the error" \
    expect_match "unparseable GUID is named in the error" 'not-a-guid.*file GUID'

# Upstream computes "body.length() - replace_length" on unsigned types, so a
# replace pattern longer than the section body underflows, passes the bounds
# check, and then reads past the end of the body while expanding placeholders.
LONG_DOTS="$(python3 -c 'print("."*800)')"
printf '%s 10 O:0:%s\n' "$HASWELL" "$LONG_DOTS" > "$WORK/oob.txt"
check "oversized offset patch is refused" \
    run 39 "oversized offset patch is refused" -- \
        "$IMAGE" "$WORK/oob.txt" -o "$WORK/out.bin"
check "oversized offset patch reports out of bounds" \
    expect_match "oversized offset patch reports out of bounds" 'out of bounds'

LONG_NOPS="$(python3 -c 'print("90"*400)')"
printf '%s 10 O:0:%s\n' "$HASWELL" "$LONG_NOPS" > "$WORK/grow.txt"
check "offset patch may not grow a section" \
    run 39 "offset patch may not grow a section" -- \
        "$IMAGE" "$WORK/grow.txt" -o "$WORK/out.bin"

# --------------------------------------------------------- patch reporting ---
# Upstream's recursive walk broke out of the loop on the first success and
# then returned ERR_NOTHING_TO_PATCH regardless, so a hit was indistinguishable
# from a miss and a half patched image exited 0.

cat > "$WORK/partial.txt" <<EOT
# applies
$HASWELL 10 P:$HASWELL_FIND:$HASWELL_REPL
# never matches
$SIINIT 12 P:DEADBEEFDEADBEEF:CAFEBABECAFEBABE
EOT

check "partial run without --strict still succeeds" \
    run 0 "partial run without --strict still succeeds" -- \
        "$IMAGE" "$WORK/partial.txt" -o "$WORK/out.bin"
check "partial run counts hits and misses" \
    expect_match "partial run counts hits and misses" 'Patches: 1 applied, 1 not matched'

check "--strict fails a partial run" \
    run 41 "--strict fails a partial run" -- \
        "$IMAGE" "$WORK/partial.txt" -o "$WORK/out.bin" --strict
check "--strict says how many lines missed" \
    expect_match "--strict says how many lines missed" '1 of 2 selected patch line'

check "--verbose names the line that missed" \
    run 0 "--verbose names the line that missed" -- \
        "$IMAGE" "$WORK/partial.txt" -o "$WORK/out.bin" -v
check "--verbose marks the applied line" \
    expect_match "--verbose marks the applied line" '\[applied\].*applies'
check "--verbose marks the missed line" \
    expect_match "--verbose marks the missed line" '\[ missed\].*never matches'

# -------------------------------------------------------------- dry run ---

rm -f "$WORK/dry.bin"
check "--dry-run exits cleanly" \
    run 0 "--dry-run exits cleanly" -- \
        "$IMAGE" "$WORK/partial.txt" -o "$WORK/dry.bin" -n
check "--dry-run says it wrote nothing" \
    expect_match "--dry-run says it wrote nothing" 'dry run, nothing written'
if [ -e "$WORK/dry.bin" ]; then
    fail "--dry-run writes no output file" "$WORK/dry.bin was created"
else
    pass "--dry-run writes no output file"
fi

# ----------------------------------------------------------- set selection ---

if [ -r "$PATCH_DB" ]; then
    check "--list exits cleanly" run 0 "--list exits cleanly" -- --list "$PATCH_DB"
    check "--list shows a set id" \
        expect_match "--list shows a set id" 'msr-e2-unlock'
    check "--list shows the risk" \
        expect_match "--list shows the risk" 'risk: medium'

    check "--set limits the run to one set" \
        run 0 "--set limits the run to one set" -- \
            "$IMAGE" "$PATCH_DB" -o "$WORK/out.bin" --set msr-e2-unlock -v
    check "--set reports the other sets as skipped" \
        expect_match "--set reports the other sets as skipped" 'skipped'
    check "--set does not run an excluded set" \
        expect_no_match "--set does not run an excluded set" 'set sgx-launch-control'

    check "an empty selection is an error" \
        run 1 "an empty selection is an error" -- \
            "$IMAGE" "$PATCH_DB" -o "$WORK/out.bin" --set no-such-set
    check "an empty selection explains itself" \
        expect_match "an empty selection explains itself" 'left no patches to apply'
else
    echo "skip  set selection tests (no $PATCH_DB)"
fi

# --------------------------------------------------------------- --all-copies ---
# The fixture carries PowerMgmtDxe twice, the way a real image carries a
# module in more than one volume.

SPEC="$HASWELL 10 P:$HASWELL_FIND:$HASWELL_REPL"
check "single copy run succeeds" \
    run 0 "single copy run succeeds" -- "$IMAGE" -p "$SPEC" -o "$WORK/one.bin" -v
check "single copy run touches one section" \
    expect_match "single copy run touches one section" '1 section\(s\) changed'

check "--all-copies run succeeds" \
    run 0 "--all-copies run succeeds" -- \
        "$IMAGE" -p "$SPEC" -o "$WORK/all.bin" -v --all-copies
check "--all-copies touches both sections" \
    expect_match "--all-copies touches both sections" '2 section\(s\) changed'

if cmp -s "$WORK/one.bin" "$WORK/all.bin"; then
    fail "--all-copies produces a different image" "the two outputs are identical"
else
    pass "--all-copies produces a different image"
fi

# ------------------------------------------------------------- input guard ---

cp "$IMAGE" "$WORK/inplace.bin"
check "writing over the input is refused" \
    run 1 "writing over the input is refused" -- \
        "$WORK/inplace.bin" -p "$SPEC" -o "$WORK/inplace.bin"
check "the input guard explains itself" \
    expect_match "the input guard explains itself" 'overwrite the input'
if cmp -s "$IMAGE" "$WORK/inplace.bin"; then
    pass "the refused run left the input untouched"
else
    fail "the refused run left the input untouched" "the input image was modified"
fi

check "--force allows writing over the input" \
    run 0 "--force allows writing over the input" -- \
        "$WORK/inplace.bin" -p "$SPEC" -o "$WORK/inplace.bin" --force

# ----------------------------------------------------------------- hashing ---

check "--hash run succeeds" \
    run 0 "--hash run succeeds" -- "$IMAGE" -p "$SPEC" -o "$WORK/out.bin" --hash
check "--hash prints a digest for input and output" \
    expect_match "--hash prints a digest for input and output" 'SHA-(256|1) [0-9A-F]{40}'

# -------------------------------------------------- upstream compatibility ---
# The stock upstream patch file must still parse and produce the same bytes,
# so that existing scripts and patch files keep working.

if [ -r "$REPO_ROOT/UEFITool-0.28.0/UEFIPatch/patches.txt" ]; then
    check "the stock upstream patch file still works" \
        run 0 "the stock upstream patch file still works" -- \
            "$IMAGE" "$REPO_ROOT/UEFITool-0.28.0/UEFIPatch/patches.txt" -o "$WORK/stock.bin"
fi

# ------------------------------------------------------------------ result ---

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
