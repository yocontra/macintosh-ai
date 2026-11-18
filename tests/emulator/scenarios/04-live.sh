#!/bin/sh
# 04-live -- optional tier T3: a real HTTPS request through SheepShaver's
# slirp stack to api.anthropic.com.
#
# Self-skips (exit 0) unless ANTHROPIC_API_KEY is set. Proves: the Open
# Transport transport, the vendored mbedTLS engine, the WE1 trust anchor and
# the streaming decoder work end to end from inside the emulated Mac.
#
# The key is typed into the disposable VM only; prefs live on the APFS clone
# and are discarded with tests/emulator/.work.

set -eu
. "$(dirname -- "$0")/_lib.sh"

if [ -z "${ANTHROPIC_API_KEY:-}" ]; then
    echo "SKIP: 04-live needs ANTHROPIC_API_KEY in the environment"
    exit 0
fi

emu click --name splash_start
emu wait-ocr "AI Chat" --timeout 30 >/dev/null || true

emu key "cmd+,"
emu wait-ocr "API key:" --timeout 30 >/dev/null

emu click --name settings_key
emu type "$ANTHROPIC_API_KEY"
emu click --name settings_ok
sleep 1

emu click --name chat_input
emu type "Reply with exactly: PONG"

START=$(date +%s)
emu key "return"
ocr_text=$(emu wait-ocr "PONG" --timeout 120)
ELAPSED=$(( $(date +%s) - START ))

printf '%s\n' "$ocr_text"
echo "04-live: PONG seen ${ELAPSED}s after Return"
