#!/bin/sh
#
# run.sh -- emulator test entry point (plan testing tiers T2/T3).
#
#   tests/emulator/run.sh <scenario> [--record] [--keep]
#
#   --record   write the scenario's reference frames instead of asserting
#   --keep     keep tests/emulator/.work (VM clone, screenshots, emulator log)
#
# Exit codes: 0 pass, 1 assertion failed, 2 environment missing.
#
# The harness never writes outside tests/emulator/.work: it APFS-clones the
# Mac OS 9 volume from the installed SheepShaver app, modifies the clone, and
# boots that.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)

PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
export PATH

APP_NAME=${APP_NAME:-MacintoshAI}
WORK=${MAC_EMU_WORK:-$HERE/.work}
BASELINES=${MAC_BASELINES:-$HERE/baselines}

RECORD=0
KEEP=0
SCENARIO=""

usage() {
    echo "usage: $0 <scenario> [--record] [--keep]" >&2
    echo "scenarios: $(ls "$HERE/scenarios" 2>/dev/null | grep -E '^[0-9].*\.sh$' | sed 's/\.sh$//' | tr '\n' ' ')" >&2
}

for arg in "$@"; do
    case "$arg" in
        --record) RECORD=1 ;;
        --keep) KEEP=1 ;;
        -h|--help) usage; exit 0 ;;
        -*)
            echo "error: unknown option $arg" >&2
            usage
            exit 2
            ;;
        *)
            if [ -n "$SCENARIO" ]; then
                echo "error: only one scenario at a time" >&2
                usage
                exit 2
            fi
            SCENARIO=$arg
            ;;
    esac
done

[ -n "$SCENARIO" ] || { usage; exit 2; }

SCENARIO_FILE="$HERE/scenarios/$SCENARIO.sh"
if [ ! -f "$SCENARIO_FILE" ]; then
    echo "error: no such scenario: $SCENARIO" >&2
    usage
    exit 2
fi

if [ -n "${PYTHON:-}" ]; then
    :
elif [ -x "$HERE/.venv/bin/python3" ]; then
    PYTHON="$HERE/.venv/bin/python3"
elif [ -x /opt/homebrew/bin/python3 ]; then
    PYTHON=/opt/homebrew/bin/python3
else
    PYTHON=python3
fi

if [ ! -f "$ROOT/dist/$APP_NAME.APPL" ]; then
    echo "error: $ROOT/dist/$APP_NAME.APPL not found -- run make build first" >&2
    exit 2
fi

if ! command -v "$PYTHON" >/dev/null 2>&1; then
    echo "error: python3 not found ($PYTHON)" >&2
    exit 2
fi

command -v tesseract >/dev/null 2>&1 || {
    echo "error: tesseract not found -- run: brew install tesseract" >&2
    exit 2
}

mkdir -p "$WORK" "$BASELINES"

export MAC_EMU_WORK="$WORK"
export MAC_BASELINES="$BASELINES"
export MAC_RECORD="$RECORD"
export APP_NAME
export PYTHON

# Preflight: pyobjc/Pillow, tesseract, Accessibility permission.
"$PYTHON" "$HERE/driver.py" preflight || exit $?

# Build the throwaway VM (APFS clone + Startup Items install).
sh "$HERE/vm/make_vm.sh" || exit $?

# Boot it. driver.py probes the .sheepvm-bundle launch form first and falls
# back to --config <prefs>; the winner is recorded in .work/launch-form.
"$PYTHON" "$HERE/driver.py" start --vm "$WORK/Test.sheepvm" || exit $?

RC=0
sh "$SCENARIO_FILE" || RC=$?

# Leave the emulator log and screenshots behind after a failure; --keep keeps
# them after a pass too.
"$PYTHON" "$HERE/driver.py" quit >/dev/null 2>&1 || true
if [ "$KEEP" -eq 0 ] && [ "$RC" -eq 0 ]; then
    rm -rf "$WORK"
else
    echo "kept $WORK (emulator log: $WORK/sheepshaver.log)"
fi

if [ "$RC" -eq 0 ]; then
    echo "PASS: $SCENARIO"
else
    echo "FAIL: $SCENARIO (exit $RC)" >&2
fi
exit "$RC"
