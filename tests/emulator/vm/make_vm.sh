#!/bin/sh
#
# make_vm.sh -- build a disposable SheepShaver VM for the test harness.
#
# 1. APFS-clone the Mac OS 9 boot volume into tests/emulator/.work/ (instant,
#    space-free, never touches the user's real sparsebundle).
# 2. Install dist/<App>.APPL into "System Folder:Startup Items:<App>" so the
#    application launches at boot. Retro68 writes the data fork to .APPL and
#    the resource fork + Finder info to %<App>.ad (AppleDouble); this script
#    merges them and checks the result lands on the HFS volume.
# 3. Write .work/Test.sheepvm/prefs from vm/prefs.tmpl plus a symlink to the
#    ROM, so driver.py can boot the clone with a 1024x768 window on slirp.
#
# Nothing here mounts the real volume read-write: it only reads it, then
# attaches and modifies the clone.
#
# Exit codes: 0 ok, 2 environment missing / install failed.

set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
EMU=$(dirname "$HERE")
ROOT=$(CDPATH= cd -- "$EMU/../.." && pwd)

APP_NAME=${APP_NAME:-MacintoshAI}
PYTHON=${PYTHON:-python3}
TITLE=${MAC_EMU_TITLE:-"Mac OS 9 Test"}
WORK=${MAC_EMU_WORK:-$EMU/.work}

VM="$WORK/Test.sheepvm"
CLONE="$WORK/MacOS9Test.sparsebundle"
MNT="$WORK/mnt"
SHARE="$WORK/share"
STAGE="$WORK/stage/$APP_NAME"

fail() {
    echo "error: $*" >&2
    exit 2
}

command -v "$PYTHON" >/dev/null 2>&1 || fail "python3 not found"

# --------------------------------------------------------------------------
# Find the user's SheepShaver VM (read-only source)
# --------------------------------------------------------------------------

SRC_VM=${MAC_VM_DIR:-}
if [ -z "$SRC_VM" ]; then
    for candidate in \
        "$HOME/Library/Application Support/MacOS9/MacOS.sheepvm" \
        "/Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm"
    do
        if [ -f "$candidate/prefs" ]; then
            SRC_VM=$candidate
            break
        fi
    done
fi
[ -n "$SRC_VM" ] || fail "no SheepShaver .sheepvm found (set MAC_VM_DIR to one with a prefs file)"
[ -f "$SRC_VM/prefs" ] || fail "no prefs file in $SRC_VM"

pref_value() {
    awk -v key="$1" '$1 == key { $1 = ""; sub(/^[ \t]+/, ""); print; exit }' "$SRC_VM/prefs"
}

DISK_SRC=$(pref_value disk)
ROM_SRC=$(pref_value rom)

case "$DISK_SRC" in
    "") fail "no 'disk' entry in $SRC_VM/prefs" ;;
    /*) ;;
    *) DISK_SRC="$SRC_VM/$DISK_SRC" ;;
esac
case "$ROM_SRC" in
    "") ROM_SRC="Mac OS ROM" ;;
    /*) ;;
    *) ROM_SRC="$SRC_VM/$ROM_SRC" ;;
esac
[ -e "$DISK_SRC" ] || fail "boot volume not found: $DISK_SRC"
[ -f "$ROM_SRC" ] || fail "Mac OS ROM not found: $ROM_SRC"

APPL="$ROOT/dist/$APP_NAME.APPL"
AD="$ROOT/dist/%$APP_NAME.ad"

[ -f "$APPL" ] || fail "$APPL is missing -- run 'make build' first"
if [ ! -f "$AD" ]; then
    # A macOS-native build may have put the resource fork on the .APPL itself;
    # a container build always needs the %<App>.ad companion.
    xattr "$APPL" 2>/dev/null | grep -q 'com.apple.ResourceFork' \
        || fail "$AD is missing and $APPL has no resource fork -- rebuild with 'make build'"
fi

# --------------------------------------------------------------------------
# Clone the boot volume (APFS clone: instant, no extra space)
# --------------------------------------------------------------------------

# A crashed previous run may have left the clone mounted; unmount before
# deleting the clone directory.
if mount | grep -F " on $MNT " >/dev/null 2>&1; then
    diskutil unmount force "$MNT" >/dev/null 2>&1 \
        || hdiutil detach "$MNT" -force >/dev/null 2>&1 || true
fi

rm -rf "$VM" "$CLONE"
mkdir -p "$WORK" "$VM" "$SHARE" "$MNT"
# A desktop alias on this volume points at "Unix:SendToMacOS9"; providing it
# stops the Finder raising "File ... wasn't found" and blocking startup.
mkdir -p "$SHARE/SendToMacOS9" "$WORK/shots" "$WORK/tmp"

if ! cp -cR "$DISK_SRC" "$CLONE" 2>/dev/null; then
    rm -rf "$CLONE"
    fail "APFS clone of $DISK_SRC failed (the volume must be on an APFS filesystem; refusing a 4 GB full copy)"
fi
# A stale lock from an unclean shutdown would stop hdiutil attaching the clone.
rm -f "$CLONE/lock"

# DiskImages rewrites a version-1 sparsebundle's Info.plist to
# `bundle-backingstore-version 2` (and adds a uuid) the moment the bundle is
# attached read-write. SheepShaver only understands version 1 and refuses the
# volume with "sparsebundle: Bad version", so keep the original bytes and put
# them back once the clone is detached again.
cp "$CLONE/Info.plist" "$WORK/Info.plist.v1"
[ -f "$CLONE/Info.bckup" ] && cp "$CLONE/Info.bckup" "$WORK/Info.bckup.v1"

# --------------------------------------------------------------------------
# Render prefs and link the ROM
# --------------------------------------------------------------------------

"$PYTHON" - "$HERE/prefs.tmpl" "$VM/prefs" "$CLONE" "$ROM_SRC" "$SHARE" "$TITLE" <<'PY'
import sys

template, out_path, disk, rom, share, title = sys.argv[1:7]
with open(template, "r", encoding="utf-8") as f:
    text = f.read()
for key, value in (("{{DISK}}", disk), ("{{ROM}}", rom),
                   ("{{SHARE}}", share), ("{{TITLE}}", title)):
    text = text.replace(key, value)
with open(out_path, "w", encoding="utf-8") as f:
    f.write(text)
PY
ln -sf "$ROM_SRC" "$VM/Mac OS ROM"

# --------------------------------------------------------------------------
# Install the application into the clone's Startup Items
# --------------------------------------------------------------------------

cleanup() {
    diskutil unmount force "$MNT" >/dev/null 2>&1 \
        || hdiutil detach "$MNT" -force >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

# `diskutil image attach` is the only non-interactive form on current macOS:
# hdiutil prompts "There may be a problem with this disk image" for this
# sparsebundle (no checksum) and cancels when stdin is not a terminal.
if ! diskutil image attach --mountOptions nobrowse --mountPoint "$MNT" "$CLONE" >/dev/null 2>&1; then
    hdiutil attach -noverify -nobrowse -readwrite -noautoopen -mountpoint "$MNT" "$CLONE" >/dev/null 2>&1 \
        || fail "could not attach the cloned volume"
fi

[ -d "$MNT/System Folder" ] || fail "cloned volume has no 'System Folder' (wrong disk image?)"

mkdir -p "$WORK/stage"
rm -f "$STAGE"

# Rebuild the real file (data + resource fork + Finder info) from
# dist/<App>.APPL + dist/%<App>.ad. Retro68 writes AppleDouble in the build
# container, so the .APPL alone is data-fork-only and will not launch.
"$PYTHON" - "$APPL" "$AD" "$STAGE" <<'PY'
import ctypes
import os
import struct
import sys

data_path, ad_path, dest = sys.argv[1:4]

libc = ctypes.CDLL(None, use_errno=True)
libc.setxattr.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
                          ctypes.c_size_t, ctypes.c_uint32, ctypes.c_int]
libc.setxattr.restype = ctypes.c_int


def set_xattr(path, name, value):
    buf = ctypes.create_string_buffer(value, len(value))
    rc = libc.setxattr(path.encode(), name.encode(), buf, len(value), 0, 0)
    if rc != 0:
        errno = ctypes.get_errno()
        raise OSError(errno, os.strerror(errno), path)


resource = b""
finder_info = b""

if os.path.exists(ad_path):
    with open(ad_path, "rb") as f:
        ad = f.read()
    if len(ad) < 26 or struct.unpack(">I", ad[0:4])[0] != 0x00051607:
        sys.exit("error: %s is not an AppleDouble file" % ad_path)
    count = struct.unpack(">H", ad[24:26])[0]
    for i in range(count):
        entry_id, offset, length = struct.unpack(">III", ad[26 + 12 * i:38 + 12 * i])
        if entry_id == 2:
            resource = ad[offset:offset + length]
        elif entry_id == 9:
            finder_info = ad[offset:offset + length]

# Fall back to the resource fork already stored on the .APPL (macOS build).
if not resource:
    try:
        import subprocess

        raw = subprocess.run(["xattr", "-px", "com.apple.ResourceFork", data_path],
                             check=False, capture_output=True).stdout
        if raw.strip():
            resource = bytes.fromhex(raw.decode().replace("\n", ""))
    except Exception:
        pass

with open(data_path, "rb") as src, open(dest, "wb") as out:
    out.write(src.read())

if resource:
    set_xattr(dest, "com.apple.ResourceFork", resource)
if finder_info:
    set_xattr(dest, "com.apple.FinderInfo", finder_info[:32])

print("staged %s: data %d bytes, resource fork %d bytes, Finder info %d bytes"
      % (dest, os.path.getsize(dest), len(resource), len(finder_info)), file=sys.stderr)
PY

STARTUP="$MNT/System Folder/Startup Items"
mkdir -p "$STARTUP"
rm -rf "$STARTUP/$APP_NAME"
ditto "$STAGE" "$STARTUP/$APP_NAME"

# ditto normally carries both forks onto HFS; if the resource fork did not
# survive, write the xattrs straight onto the HFS file (same bytes).
if ! xattr "$STARTUP/$APP_NAME" 2>/dev/null | grep -q 'com.apple.ResourceFork'; then
    "$PYTHON" - "$STAGE" "$STARTUP/$APP_NAME" <<'PY'
import ctypes
import os
import subprocess
import sys

src, dest = sys.argv[1:3]
libc = ctypes.CDLL(None, use_errno=True)
libc.setxattr.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
                          ctypes.c_size_t, ctypes.c_uint32, ctypes.c_int]
libc.setxattr.restype = ctypes.c_int


def read_xattr(path, name):
    raw = subprocess.run(["xattr", "-px", name, path], check=False,
                         capture_output=True).stdout
    if not raw.strip():
        return None
    return bytes.fromhex(raw.decode().replace("\n", ""))


def set_xattr(path, name, value):
    buf = ctypes.create_string_buffer(value, len(value))
    if libc.setxattr(path.encode(), name.encode(), buf, len(value), 0, 0) != 0:
        errno = ctypes.get_errno()
        raise OSError(errno, os.strerror(errno), path)


for attr in ("com.apple.ResourceFork", "com.apple.FinderInfo"):
    value = read_xattr(src, attr)
    if value:
        set_xattr(dest, attr, value)
PY
fi

xattr "$STARTUP/$APP_NAME" 2>/dev/null | grep -q 'com.apple.ResourceFork' \
    || fail "resource fork did not survive the copy to $STARTUP/$APP_NAME"

TYPE=$(GetFileInfo -t "$STARTUP/$APP_NAME" 2>/dev/null | tr -d '"' || true)
CREATOR=$(GetFileInfo -c "$STARTUP/$APP_NAME" 2>/dev/null | tr -d '"' || true)
EXPECTED_CREATOR=${EMU_CREATOR:-McAI}
[ "$TYPE" = "APPL" ] || fail "installed file has type '$TYPE', expected APPL"
[ "$CREATOR" = "$EXPECTED_CREATOR" ] || fail "installed file has creator '$CREATOR', expected $EXPECTED_CREATOR"

# Drop any preferences left on the source volume so every run starts with an
# empty API key / default provider. (The clone is disposable.)
find "$MNT/System Folder/Preferences" -maxdepth 1 -name "$APP_NAME Prefs" -exec rm -f {} \; 2>/dev/null || true

# The Retro68 LaunchAPPL server is optional: only install it when it has been
# built locally (it is not needed when the app starts from Startup Items).
SERVER=$(find "$HOME/Projects/Retro68" -type f -name LaunchAPPLServer 2>/dev/null | head -1)
if [ -n "$SERVER" ]; then
    ditto "$SERVER" "$STARTUP/LaunchAPPLServer" 2>/dev/null || true
fi

# The in-guest self-test: the application looks for this file at startup and,
# if it is there, drives its own UI, writes "MacintoshAI selftest log" beside
# it and quits. MAC_EMU_SELFTEST=1 asks for it. No input events are involved,
# so the harness never touches the real mouse or keyboard.
# Either the environment or a marker file asks for it, so a scenario runner that
# builds the VM in a subprocess with its own environment can still request it.
if [ "${MAC_EMU_SELFTEST:-0}" = "1" ] || [ -f "$WORK/request-selftest" ]; then
    PREFSDIR="$MNT/System Folder/Preferences"
    mkdir -p "$PREFSDIR"
    : > "$PREFSDIR/$APP_NAME selftest"
    [ -f "$PREFSDIR/$APP_NAME selftest" ] || fail "could not create the selftest trigger"
    echo "make_vm: selftest trigger in System Folder:Preferences"
else
    rm -f "$MNT/System Folder/Preferences/$APP_NAME selftest" 2>/dev/null || true
fi

trap - EXIT INT TERM
diskutil unmount force "$MNT" >/dev/null 2>&1 \
    || hdiutil detach "$MNT" -force >/dev/null 2>&1 \
    || fail "could not detach $MNT"

# Undo the DiskImages format upgrade so SheepShaver can read the clone.
cp "$WORK/Info.plist.v1" "$CLONE/Info.plist"
[ -f "$WORK/Info.bckup.v1" ] && cp "$WORK/Info.bckup.v1" "$CLONE/Info.bckup"
rm -f "$CLONE/lock"

echo "VM ready: $VM"
echo "  disk  $CLONE (APFS clone)"
echo "  app   System Folder:Startup Items:$APP_NAME (type $TYPE, creator $CREATOR)"
echo "  share $SHARE"
