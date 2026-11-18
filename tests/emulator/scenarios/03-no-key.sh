#!/bin/sh
# 03-no-key -- Start Chat, type a prompt, press Return with no API key
# configured. The transcript must report the missing key, and the app must
# stay responsive afterwards.
#
# Proves: the chat window opens from the splash button, Return submits,
# API_BeginRequest fails cleanly without a key (no hang, no crash), and input
# still works.

set -eu
. "$(dirname -- "$0")/_lib.sh"

emu click --name splash_start   # also brings the app to the front
emu wait-ocr "AI Chat" --timeout 30 >/dev/null || true

emu click --name chat_input
emu type "hello"
emu key "return"

ocr_text=$(emu wait-ocr "API key" --timeout 30)
printf '%s\n' "$ocr_text"

# Still responsive: typing into the input field changes that region. (The
# before frame is a per-run shot, not a checked-in baseline.)
before="$WORK/shots/before-03.png"
emu shot before-03 --region chat_input --out "$before" >/dev/null
emu click --name chat_input
emu type "x"
emu changed chat_input "$before" 0.0005
