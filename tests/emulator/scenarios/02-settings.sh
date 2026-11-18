#!/bin/sh
# 02-settings -- Cmd-, opens the settings dialog, it shows the four fields,
# it visibly changes the screen, and closing it returns to the splash frame.
#
# Proves: the File>Settings menu item and DLOG 140 are wired, the Dialog
# Manager modal loop runs, and the app redraws the splash afterwards.

set -eu
. "$(dirname -- "$0")/_lib.sh"

# The Finder is frontmost after boot; click the splash window (not the button)
# so the menu command reaches the app.
emu click 512 190
emu key "cmd+,"
ocr_text=$(emu wait-ocr "Model:" --timeout 30)
printf '%s\n' "$ocr_text"
assert_contains "$ocr_text" "API key:" "settings: dialog fields"

emu changed splash "$BASELINES/launch.png" 0.05

# ModalDialog has no built-in Esc handling (unless the app installs a filter
# proc), so give Esc a chance and fall back to the Cancel button.
emu key "esc"
if ! emu wait-stable splash "$BASELINES/launch.png" 0.02 --timeout 3; then
    echo "note: Esc did not close the settings dialog; clicking Cancel"
    emu click --name settings_cancel
    emu wait-stable splash "$BASELINES/launch.png" 0.02 --timeout 10
fi
