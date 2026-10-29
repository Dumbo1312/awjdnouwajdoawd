#!/usr/bin/env bash
# GUI regression tests for the theme and the preset dialog.
#
# Builds tests/guicheck, which links the same sources as UEFITool but with its
# own main(), and runs it under Qt's "offscreen" platform plugin so no X server
# is needed. It also cross-checks that applying a preset through the GUI's code
# path produces exactly the image UEFIPatch produces from the same preset.
#
# Screenshots of the themed windows are written to the output directory, which
# is printed at the end.
#
# Usage: tests/run_gui_tests.sh [output-directory]

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

SHOTS="${1:-$WORK/shots}"
mkdir -p "$SHOTS"

PRESETS="$REPO_ROOT/patches/presets.txt"
DEBLOAT="$REPO_ROOT/knowledge/debloat.txt"

QMAKE=""
for candidate in qmake /usr/lib/qt5/bin/qmake qmake-qt5 qmake6; do
    if command -v "$candidate" > /dev/null 2>&1; then QMAKE="$candidate"; break; fi
    if [ -x "$candidate" ]; then QMAKE="$candidate"; break; fi
done
if [ -z "$QMAKE" ]; then
    echo "no qmake found; install Qt to run the GUI tests" >&2
    exit 2
fi

echo "Building guicheck and UEFIPatch with $QMAKE ..."

mkdir -p "$WORK/gui" "$WORK/cli"
( cd "$WORK/gui" \
    && "$QMAKE" "$REPO_ROOT/tests/guicheck/guicheck.pro" > /dev/null \
    && make -j"$(nproc 2>/dev/null || echo 2)" > "$WORK/gui-build.log" 2>&1 ) \
    || { echo "guicheck build failed:" >&2; cat "$WORK/gui-build.log" >&2; exit 2; }

( cd "$WORK/cli" \
    && "$QMAKE" "$REPO_ROOT/UEFITool-0.28.0/UEFIPatch/uefipatch.pro" > /dev/null \
    && make -j"$(nproc 2>/dev/null || echo 2)" > "$WORK/cli-build.log" 2>&1 ) \
    || { echo "UEFIPatch build failed:" >&2; cat "$WORK/cli-build.log" >&2; exit 2; }

IMAGE="$WORK/test.bin"
python3 "$REPO_ROOT/tests/make_test_image.py" "$IMAGE" > /dev/null

echo
# The offscreen plugin warns about propagateSizeHints() and XDG_RUNTIME_DIR on
# every window; neither matters here, so they are filtered out of the report.
QT_QPA_PLATFORM=offscreen "$WORK/gui/guicheck" "$IMAGE" "$PRESETS" "$DEBLOAT" "$SHOTS" 2>&1 \
    | grep -vE '^parseSection|propagateSizeHints|XDG_RUNTIME_DIR'
GUI_RESULT=${PIPESTATUS[0]}

echo
echo "Cross-checking the GUI apply path against UEFIPatch"

"$WORK/cli/UEFIPatch" "$IMAGE" "$PRESETS" \
    --set intel-cstate-unlock --all-copies -o "$WORK/cli-applied.bin" > /dev/null 2>&1
CLI_RESULT=$?

CROSS_RESULT=0
if [ "$CLI_RESULT" != 0 ]; then
    echo "FAIL  UEFIPatch could not apply the preset (exit $CLI_RESULT)"
    CROSS_RESULT=1
elif [ ! -f "$SHOTS/gui-applied.bin" ]; then
    echo "FAIL  the GUI apply path produced no image"
    CROSS_RESULT=1
elif cmp -s "$SHOTS/gui-applied.bin" "$WORK/cli-applied.bin"; then
    echo "ok    the GUI and UEFIPatch produce a byte-identical image"
else
    echo "FAIL  the GUI and UEFIPatch produced different images"
    CROSS_RESULT=1
fi

rm -f "$SHOTS/gui-applied.bin"

echo
if [ "$GUI_RESULT" -eq 0 ] && [ "$CROSS_RESULT" -eq 0 ]; then
    echo "GUI tests passed. Screenshots in $SHOTS"
    exit 0
fi

echo "GUI tests failed."
exit 1
