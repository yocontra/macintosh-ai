#!/bin/sh
#
# selftest.sh -- run the application's in-guest self-test.
#
# The application drives its own UI, writes a log beside the trigger file and
# quits. Nothing here posts a mouse or keyboard event: the harness only boots
# the VM, waits, quits it and reads the log off the clone. That means it does
# not move the cursor of whoever is using this Mac, and it cannot fail because
# a synthetic click was lost in a modal tracking loop.
#
#   tests/emulator/selftest.sh            run it, print the log, assert "done"
#   tests/emulator/selftest.sh --keep     leave .work/ in place
#
# Exit codes: 0 the log ends with done and no "=0" failure marker,
#             1 an assertion failed, 2 environment missing.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
WORK="$HERE/.work"
MNT="$WORK/mnt-selftest"
KEEP=0

[ "${1:-}" = "--keep" ] && KEEP=1

APP_NAME=MacintoshAI
LOG_NAME="$APP_NAME selftest log"

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
    if [ -d "$MNT" ] && mount | grep -F " on $MNT " >/dev/null 2>&1; then
        diskutil unmount force "$MNT" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

echo "== building the VM with the selftest trigger"
MAC_EMU_SELFTEST=1 APP_NAME="$APP_NAME" sh "$HERE/vm/make_vm.sh" || exit 2

echo "== booting"
"$PYTHON" "$HERE/driver.py" start --vm "$WORK/Test.sheepvm" >/dev/null || exit 2

# Boot is ~45 s and the scripted UI work is ~25 s; the app quits when it is
# done, leaving the Finder on screen. Nothing to poll: the log is on a volume
# the guest has mounted.
sleep 90

echo "== stopping the VM"
"$PYTHON" "$HERE/driver.py" quit >/dev/null 2>&1 || true
sleep 2

echo "== reading the log"
SHARE_LOG="$WORK/share/$LOG_NAME"
if [ -f "$SHARE_LOG" ]; then
    echo "   (from the extfs share)"
    tr '\r' '\n' < "$SHARE_LOG" | tee "$WORK/selftest.log"
    if grep -q '^done$' "$WORK/selftest.log"; then
        echo "PASS: selftest"
        exit 0
    fi
    echo "FAIL: selftest log has no 'done' line" >&2
    exit 1
fi

mkdir -p "$MNT"
diskutil image attach --mountOptions nobrowse --mountPoint "$MNT" \
    "$WORK/MacOS9Test.sparsebundle" >/dev/null 2>&1 \
    || hdiutil attach -noverify -nobrowse -readonly -mountpoint "$MNT" \
        "$WORK/MacOS9Test.sparsebundle" >/dev/null 2>&1 \
    || { echo "error: could not mount the clone" >&2; exit 2; }

LOG_FILE="$MNT/System Folder/Preferences/$LOG_NAME"
if [ ! -f "$LOG_FILE" ]; then
    echo "FAIL: the application did not write $LOG_NAME" >&2
    diskutil unmount force "$MNT" >/dev/null 2>&1 || true
    exit 1
fi

# CR line endings, as the app writes them.
tr '\r' '\n' < "$LOG_FILE" | tee "$WORK/selftest.log"
diskutil unmount force "$MNT" >/dev/null 2>&1 || true
rmdir "$MNT" 2>/dev/null || true

[ "$KEEP" = "1" ] || true

if ! grep -q '^done$' "$WORK/selftest.log"; then
    echo "FAIL: selftest log has no 'done' line (the app quit early or hung)" >&2
    exit 1
fi
if grep -q '^submit=0$' "$WORK/selftest.log"; then
    echo "FAIL: the send path refused the prompt" >&2
    exit 1
fi
if grep -q '^open_chat=0$' "$WORK/selftest.log"; then
    echo "FAIL: the chat window did not open" >&2
    exit 1
fi

echo "PASS: selftest"
