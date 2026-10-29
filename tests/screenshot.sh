#!/usr/bin/env bash
# Drives the real UEFITool binary and photographs it.
#
# Unlike tests/run_gui_tests.sh, which renders widgets offscreen from inside a
# test program, this builds UEFITool itself, runs it under a virtual X server
# and clicks through the preset flow with xdotool, so the images are of the
# program as a user sees it.
#
# Needs: Qt, Xvfb, openbox, xdotool, xwininfo, ImageMagick's import.
#   apt-get install xvfb openbox xdotool x11-utils imagemagick
#
# Usage: tests/screenshot.sh [output-directory]

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$REPO_ROOT/docs/images}"
WORK="$(mktemp -d)"
DISPLAY_NUM=":99"

cleanup() {
    pkill -f "UEFITool $WORK" 2>/dev/null
    [ -n "${OPENBOX_PID:-}" ] && kill "$OPENBOX_PID" 2>/dev/null
    [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

for tool in Xvfb openbox xdotool xwininfo import python3; do
    command -v "$tool" > /dev/null 2>&1 || { echo "missing: $tool" >&2; exit 2; }
done

QMAKE=""
for candidate in qmake /usr/lib/qt5/bin/qmake qmake-qt5 qmake6; do
    if command -v "$candidate" > /dev/null 2>&1; then QMAKE="$candidate"; break; fi
    if [ -x "$candidate" ]; then QMAKE="$candidate"; break; fi
done
[ -n "$QMAKE" ] || { echo "no qmake found" >&2; exit 2; }

mkdir -p "$OUT"

echo "Building UEFITool ..."
mkdir -p "$WORK/build"
( cd "$WORK/build" \
    && "$QMAKE" "$REPO_ROOT/UEFITool-0.28.0/uefitool.pro" > /dev/null \
    && make -j"$(nproc 2>/dev/null || echo 2)" > "$WORK/build.log" 2>&1 ) \
    || { echo "build failed:" >&2; cat "$WORK/build.log" >&2; exit 2; }

python3 "$REPO_ROOT/tests/make_test_image.py" "$WORK/demo.bin" > /dev/null

echo "Starting a virtual display ..."
Xvfb "$DISPLAY_NUM" -screen 0 1400x900x24 -nolisten tcp > /dev/null 2>&1 &
XVFB_PID=$!
sleep 2
export DISPLAY="$DISPLAY_NUM"
openbox --sm-disable > /dev/null 2>&1 &
OPENBOX_PID=$!
sleep 2

# Start from stored settings that do not exist, so the window opens themed
# and at a known size rather than wherever a previous run left it.
rm -rf "${HOME:-/root}/.config/CodeRush"

"$WORK/build/UEFITool" "$WORK/demo.bin" > "$WORK/uefitool.log" 2>&1 &
sleep 5

# Absolute position of a window's client area, which is where import starts
# and what mouse coordinates have to be measured from.
client_origin() {
    xwininfo -id "$1" \
        | awk '/Absolute upper-left X/ {x=$NF} /Absolute upper-left Y/ {y=$NF} END {print x, y}'
}

click_in() {  # click_in <window> <client x> <client y>
    local origin
    origin=$(client_origin "$1")
    xdotool mousemove $(( ${origin%% *} + $2 )) $(( ${origin##* } + $3 )) click 1
    sleep 1
}

shoot() {     # shoot <window> <file>
    sleep 1
    import -window "$1" "$OUT/$2" && echo "  wrote $OUT/$2"
}

MAIN=$(xdotool search --name "^UEFITool 0\.28\.0" | head -1)
[ -n "$MAIN" ] || { echo "the main window never appeared" >&2; cat "$WORK/uefitool.log" >&2; exit 1; }

xdotool windowmove "$MAIN" 40 40
xdotool windowsize "$MAIN" 1280 800
sleep 2

echo "Capturing the main window ..."
# Select the root of the tree and expand everything under it.
click_in "$MAIN" 80 96
xdotool key asterisk
sleep 1
shoot "$MAIN" "main-window.png"

echo "Opening the debloat dialog ..."
xdotool key ctrl+d
sleep 4
DEBLOAT=$(xdotool search --name "^Debloat$" | tail -1)
[ -n "$DEBLOAT" ] || { echo "the debloat dialog never appeared" >&2; exit 1; }
xdotool windowmove "$DEBLOAT" 55 55
xdotool windowsize "$DEBLOAT" 1000 720
sleep 2
# Walk down to CsmDxe, the fixture's module with an apriori finding, so the
# dependency panel has something to show. The fixture's order is fixed, so
# counting rows is deterministic here even though it would not be in general.
xdotool key Home; sleep 1
for i in 1 2 3 4 5 6; do xdotool key Down; sleep 1; done
shoot "$DEBLOAT" "debloat-dialog.png"
xdotool key Escape
sleep 2

echo "Opening the preset dialog ..."
xdotool key ctrl+t
sleep 3
DIALOG=$(xdotool search --name "^Performance presets$" | tail -1)
[ -n "$DIALOG" ] || { echo "the preset dialog never appeared" >&2; exit 1; }
xdotool windowmove "$DIALOG" 60 60
xdotool windowsize "$DIALOG" 940 640
sleep 2

# Driven from the keyboard rather than by pixel coordinates, which depend on
# how the preset names happen to lay out: the tree has focus when the dialog
# opens, Home goes to the first platform heading, Down to its first preset,
# and Space ticks it. Selecting it also fills the detail pane.
xdotool key Home; sleep 1
xdotool key Down; sleep 1
xdotool key space; sleep 1
shoot "$DIALOG" "preset-dialog.png"

echo "Applying the preset ..."
# Return would be swallowed by the tree, so Apply tweaks is clicked. It is
# the second button from the right, placed from the far corner so that the
# dialog's size is the only thing this depends on.
eval "$(xdotool getwindowgeometry --shell "$DIALOG")"
click_in "$DIALOG" $(( WIDTH - 153 )) $(( HEIGHT - 27 ))
sleep 3
BOX=$(xdotool search --name "^Tweaked$" | tail -1)
[ -n "$BOX" ] || { echo "the result dialog never appeared" >&2; exit 1; }
xdotool windowmove "$BOX" 90 90
sleep 1
# Show Details is the rightmost button on the bottom row, so it is placed
# from the far corner rather than from a fixed coordinate.
eval "$(xdotool getwindowgeometry --shell "$BOX")"
click_in "$BOX" $(( WIDTH - 64 )) $(( HEIGHT - 27 ))
sleep 1
shoot "$BOX" "tweaked-dialog.png"

echo "Declining the save, to photograph the result in the window ..."
# Escape maps to the No button on a Yes/No box.
xdotool key Escape
sleep 2
shoot "$MAIN" "main-window-tweaked.png"

echo
echo "Screenshots in $OUT"
