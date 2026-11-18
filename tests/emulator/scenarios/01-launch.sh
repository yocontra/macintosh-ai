#!/bin/sh
# 01-launch -- the app boots from Startup Items, shows its splash, and the
# screen settles into a reproducible frame.
#
# Proves: the Startup Items install worked (type/creator + resource fork
# survived the HFS copy), the 68k binary runs under Mac OS 9, and the splash
# renders the same thing on every run.

set -eu
. "$(dirname -- "$0")/_lib.sh"

emu wait-window --timeout 120

# "Start Chat" is the most specific text on the splash; wait for it rather
# than for the generic "AI" which the menu bar could also produce.
emu wait-ocr "Start Chat" --timeout 120 >/dev/null

ocr_text=$(emu ocr full)
printf '%s\n' "$ocr_text"
# "AI" alone is not usable: tesseract reads the splash's AI logo as "Al".
# "Start Chat" is the app's own control, and "Macintosh" only appears in the
# splash copy, so between them the assertion is about the app, not the Finder.
assert_contains "$ocr_text" "Start Chat" "launch: splash button"
assert_contains "$ocr_text" "Macintosh" "launch: splash title"

# The comparison region is the splash window, not the whole screen: the menu
# bar's clock changes every minute and nothing else on the desktop is ours.
emu stable splash "$BASELINES/launch.png" 0.02
