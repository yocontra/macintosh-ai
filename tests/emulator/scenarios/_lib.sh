# Shared shell helpers for tests/emulator/scenarios/*.sh.
#
# Sourced, not executed. run.sh exports MAC_EMU_WORK / MAC_BASELINES /
# MAC_RECORD / APP_NAME / PYTHON; the defaults here let a scenario also run by
# hand against an emulator started with driver.py.

EMU_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

PYTHON=${PYTHON:-/opt/homebrew/bin/python3}
[ -x "$PYTHON" ] || PYTHON=python3

WORK=${MAC_EMU_WORK:-$EMU_DIR/.work}
BASELINES=${MAC_BASELINES:-$EMU_DIR/baselines}
emu() {
    "$PYTHON" "$EMU_DIR/driver.py" "$@"
}

# assert_contains <ocr-text> <needle> <label>   (case-insensitive)
assert_contains() {
    haystack=$1
    needle=$2
    label=$3
    lowered_haystack=$(printf '%s' "$haystack" | tr 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' 'abcdefghijklmnopqrstuvwxyz')
    lowered_needle=$(printf '%s' "$needle" | tr 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' 'abcdefghijklmnopqrstuvwxyz')
    if printf '%s' "$lowered_haystack" | grep -F -e "$lowered_needle" >/dev/null 2>&1; then
        return 0
    fi
    echo "FAIL: $label: expected OCR text to contain '$needle'; saw:" >&2
    printf '%s\n' "$haystack" >&2
    exit 1
}
