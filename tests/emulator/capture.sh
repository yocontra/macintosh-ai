#!/bin/sh
#
# capture.sh -- photograph the guest without touching the mouse or keyboard.
#
# Boots the VM and reads the screen with CGWindowListCreateImage on a timer.
# No input events are posted, so this never moves the cursor of whoever is
# using this Mac. Use it to check what the application actually looks like.
#
#   tests/emulator/capture.sh [seconds] [interval]
#
# Frames land in tests/emulator/.work/shots/frameNN.png; the last one is copied
# to tests/emulator/.work/shots/last.png.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
WORK="$HERE/.work"
SECONDS_TO_RUN=${1:-90}
INTERVAL=${2:-3}

APP_NAME=MacintoshAI

[ -f "$ROOT/dist/$APP_NAME.APPL" ] || {
    echo "error: dist/$APP_NAME.APPL not found -- run make build first" >&2
    exit 2
}

if [ -n "${PYTHON:-}" ]; then
    :
elif [ -x "$HERE/.venv/bin/python3" ]; then
    PYTHON="$HERE/.venv/bin/python3"
else
    PYTHON=python3
fi

export MAC_EMU_WORK="$WORK"

cleanup() {
    "$PYTHON" "$HERE/driver.py" quit >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

# MAC_EMU_SELFTEST=1 boots with the in-guest self-test trigger, so the frames
# show what the scripted UI run actually paints.
MAC_EMU_SELFTEST="${MAC_EMU_SELFTEST:-0}" APP_NAME="$APP_NAME" \
    sh "$HERE/vm/make_vm.sh" >/dev/null 2>&1 || exit 2
"$PYTHON" "$HERE/driver.py" start --vm "$WORK/Test.sheepvm" >/dev/null || exit 2

echo "== capturing for ${SECONDS_TO_RUN}s every ${INTERVAL}s (no input events)"
n=0
elapsed=0
while [ "$elapsed" -lt "$SECONDS_TO_RUN" ]; do
    sleep "$INTERVAL"
    elapsed=$((elapsed + INTERVAL))
    n=$((n + 1))
    "$PYTHON" "$HERE/driver.py" shot "$(printf 'frame%02d' "$n")" >/dev/null 2>&1 || true
    echo "  frame$(printf '%02d' "$n").png at ${elapsed}s"
done

cp "$WORK/shots/$(printf 'frame%02d' "$n").png" "$WORK/shots/last.png" 2>/dev/null || true
echo "== frames in $WORK/shots"
