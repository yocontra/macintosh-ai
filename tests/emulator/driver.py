#!/usr/bin/env python3
"""driver.py -- the only thing that talks to the emulator.

Drives SheepShaver (the Mac OS 9 install at /Applications/Mac OS 9.app) through
a disposable .sheepvm created by vm/make_vm.sh:

  * capture  -- Quartz.CGWindowListCreateImage on the SheepShaver window
  * input    -- Quartz.CGEventPost (mouse + keyboard), guest coordinates
  * OCR      -- tesseract on a captured region
  * diffs    -- Pillow image comparison against recorded baselines

Coordinates are guest screen coordinates (1024x768 by default), never host
coordinates, so scenarios stay valid across host displays. The emulator window
geometry (title bar, possible scaling) is derived from the window bounds each
time, not hard-coded.

Typical use is through tests/emulator/run.sh, but every command is usable by
hand:

    driver.py start --vm .work/Test.sheepvm
    driver.py shot full
    driver.py click 512 320
    driver.py key "cmd+,"
    driver.py type "hello"
    driver.py ocr full
    driver.py stable full baselines/launch.png 0.02
    driver.py quit

Exit codes: 0 success, 1 assertion failure, 2 environment missing (no
SheepShaver, no tesseract, no TCC permission, no python modules).
"""

from __future__ import annotations

import argparse
import atexit
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

# --------------------------------------------------------------------------
# Paths and configuration
# --------------------------------------------------------------------------

EMU_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(EMU_DIR))

WORK_DIR = os.environ.get("MAC_EMU_WORK") or os.path.join(EMU_DIR, ".work")
BASELINE_DIR = os.environ.get("MAC_BASELINES") or os.path.join(EMU_DIR, "baselines")
COORDS_FILE = os.environ.get("MAC_COORDS") or os.path.join(EMU_DIR, "scenarios", "coords.json")

GUEST_W = int(os.environ.get("MAC_GUEST_W", "1024"))
GUEST_H = int(os.environ.get("MAC_GUEST_H", "768"))

WINDOW_TITLE = os.environ.get("MAC_EMU_TITLE", "Mac OS 9 Test")
RECORD = os.environ.get("MAC_RECORD", "0") == "1"

SHEEPSHAVER = os.environ.get(
    "MAC_SHEEPSHAVER",
    "/Applications/Mac OS 9.app/Contents/Resources/Files/"
    "SheepShaver.app/Contents/MacOS/SheepShaver",
)

PIDFILE = os.path.join(WORK_DIR, "emulator.json")
LOG_FILE = os.path.join(WORK_DIR, "sheepshaver.log")

DIFF_THRESHOLD = 24  # per-pixel luminance delta that counts as "different"

# --------------------------------------------------------------------------
# Optional heavy imports (pyobjc / Pillow). Imported lazily so that
# `python3 -m py_compile driver.py` works on a machine without them.
# --------------------------------------------------------------------------

_IMPORT_ERROR = None
try:
    import Quartz
    from PIL import Image, ImageChops
except Exception as _exc:  # pragma: no cover - environment dependent
    Quartz = None  # type: ignore[assignment]
    Image = None  # type: ignore[assignment]
    ImageChops = None  # type: ignore[assignment]
    _IMPORT_ERROR = _exc

TCC_MESSAGE = "grant Accessibility + Screen Recording to this process"


class HarnessError(Exception):
    def __init__(self, message: str, code: int = 1):
        super().__init__(message)
        self.code = code


# --------------------------------------------------------------------------
# Small helpers
# --------------------------------------------------------------------------


def ensure_dirs() -> None:
    for d in (WORK_DIR, os.path.join(WORK_DIR, "shots"), os.path.join(WORK_DIR, "tmp"),
              BASELINE_DIR):
        os.makedirs(d, exist_ok=True)


def require_modules() -> None:
    if _IMPORT_ERROR is not None:
        raise HarnessError(
            "python modules missing (%s) -- run: python3 -m pip install -r "
            "tests/emulator/requirements.txt" % _IMPORT_ERROR,
            2,
        )
    if shutil.which("tesseract") is None:
        raise HarnessError("tesseract not found -- run: brew install tesseract", 2)


def read_coords() -> dict:
    try:
        with open(COORDS_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except OSError as exc:
        raise HarnessError("cannot read %s: %s" % (COORDS_FILE, exc), 2)
    except ValueError as exc:
        raise HarnessError("%s is not valid JSON: %s" % (COORDS_FILE, exc), 2)


def region(name: str):
    coords = read_coords()
    try:
        x, y, w, h = coords["regions"][name]
    except (KeyError, TypeError, ValueError):
        raise HarnessError("unknown region %r in %s" % (name, COORDS_FILE), 2)
    return int(x), int(y), int(w), int(h)


def point(name: str):
    coords = read_coords()
    try:
        x, y = coords["points"][name]
    except (KeyError, TypeError, ValueError):
        raise HarnessError("unknown point %r in %s" % (name, COORDS_FILE), 2)
    return int(x), int(y)


def _ax_trusted() -> bool:
    """Accessibility trust, via ApplicationServices (ctypes, no extra module)."""
    try:
        import ctypes

        lib = ctypes.CDLL(
            "/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices"
        )
        lib.AXIsProcessTrusted.restype = ctypes.c_bool
        return bool(lib.AXIsProcessTrusted())
    except Exception:
        return True  # cannot determine; the capture/cursor checks still run


# --------------------------------------------------------------------------
# Window discovery and geometry
# --------------------------------------------------------------------------


def _window_list():
    options = (
        Quartz.kCGWindowListOptionOnScreenOnly
        | Quartz.kCGWindowListExcludeDesktopElements
    )
    return Quartz.CGWindowListCopyWindowInfo(options, Quartz.kCGNullWindowID) or []


def find_window(title: str | None = None):
    """Largest SheepShaver window, preferring one whose title matches."""
    best = None
    best_score = 99
    for info in _window_list():
        try:
            bounds = info["kCGWindowBounds"]
            width = float(bounds["Width"])
            height = float(bounds["Height"])
        except (KeyError, TypeError):
            continue
        if width < 320 or height < 240:
            continue

        owner = str(info.get("kCGWindowOwnerName", ""))
        name = str(info.get("kCGWindowName", ""))
        if title and name == title:
            score = 0
        elif "sheepshaver" in owner.lower():
            score = 1
        else:
            continue

        if score < best_score or (
            score == best_score and best is not None
            and width * height > best["bounds"]["Width"] * best["bounds"]["Height"]
        ):
            best = {
                "id": info["kCGWindowNumber"],
                "bounds": bounds,
                "name": name,
                "owner": owner,
            }
            best_score = score
    return best


def wait_window(timeout: float, title: str | None = None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        win = find_window(title)
        if win is not None:
            return win
        time.sleep(0.5)
    raise HarnessError(
        "SheepShaver window %r did not appear within %.0fs (see %s)"
        % (title or WINDOW_TITLE, timeout, LOG_FILE),
        2,
    )


def geometry(win: dict) -> dict:
    """Guest-screen -> host mapping, derived from the window bounds.

    SheepShaver's window is the guest screen scaled to fit, plus a title bar.
    The guest content is centred horizontally and sits at the bottom of the
    window (the title bar occupies the top).
    """
    b = win["bounds"]
    bw, bh = float(b["Width"]), float(b["Height"])
    scale = min(bw / GUEST_W, bh / GUEST_H)
    content_w, content_h = GUEST_W * scale, GUEST_H * scale
    origin_x = (bw - content_w) / 2.0
    origin_y = bh - content_h  # title bar height in host points
    return {
        "x": float(b["X"]),
        "y": float(b["Y"]),
        "scale": scale,
        "origin_x": origin_x,
        "origin_y": origin_y,
    }


def guest_to_host(win: dict, gx: float, gy: float):
    g = geometry(win)
    return (
        g["x"] + g["origin_x"] + gx * g["scale"],
        g["y"] + g["origin_y"] + gy * g["scale"],
    )


# --------------------------------------------------------------------------
# Capture
# --------------------------------------------------------------------------


def _capture_cgimage(win: dict):
    image = Quartz.CGWindowListCreateImage(
        Quartz.CGRectNull,
        Quartz.kCGWindowListOptionIncludingWindow,
        win["id"],
        Quartz.kCGWindowImageBoundsIgnoreFraming,
    )
    if image is None or Quartz.CGImageGetWidth(image) == 0:
        raise HarnessError(
            "CGWindowListCreateImage returned no image -- " + TCC_MESSAGE, 2
        )
    return image


def _cgimage_to_pil(cgimage):
    width = Quartz.CGImageGetWidth(cgimage)
    height = Quartz.CGImageGetHeight(cgimage)
    bits_per_pixel = Quartz.CGImageGetBitsPerPixel(cgimage)
    bytes_per_row = Quartz.CGImageGetBytesPerRow(cgimage)

    if bits_per_pixel != 32:
        raise HarnessError(
            "unexpected window capture format (%d bits per pixel)" % bits_per_pixel, 2
        )

    provider = Quartz.CGImageGetDataProvider(cgimage)
    data = Quartz.CGDataProviderCopyData(provider)
    raw = bytes(data)
    return Image.frombuffer("RGBA", (width, height), raw, "raw", "BGRA", bytes_per_row, 1)


def capture(win: dict | None = None):
    require_modules()
    win = win or wait_window(0.1)
    return _cgimage_to_pil(_capture_cgimage(win))


def guest_rect_to_image_rect(win: dict, image, gx, gy, gw, gh):
    """Guest rectangle -> pixels in a captured window image.

    CGWindowListCreateImage returns the window *without* its frame when it
    honours kCGWindowImageBoundsIgnoreFraming, and *with* it (title bar
    included) when it falls back to the on-screen rectangle. Both happen on
    this machine depending on whether the window is key, so the offset and
    scale are derived from the image actually returned rather than assumed:
    the guest screen is always the bottom GUEST_W:GUEST_H rectangle of it.
    """
    px = image.width / float(GUEST_W)          # image pixels per guest pixel
    guest_px_w = GUEST_W * px
    guest_px_h = GUEST_H * px
    left_pad = (image.width - guest_px_w) / 2.0
    top = image.height - guest_px_h            # title bar, 0 when unframed
    left = left_pad + gx * px
    top = top + gy * px
    right = left + gw * px
    bottom = top + gh * px
    return tuple(int(round(v)) for v in (left, top, right, bottom))


def capture_region(name: str):
    win = wait_window(0.1)
    image = capture(win)
    x, y, w, h = region(name)
    box = guest_rect_to_image_rect(win, image, x, y, w, h)
    return image.crop(box)


# --------------------------------------------------------------------------
# Input
# --------------------------------------------------------------------------


def _emulator_pid() -> int | None:
    try:
        with open(PIDFILE, "r", encoding="utf-8") as f:
            return int(json.load(f)["emulator"])
    except Exception:
        return None


def activate_emulator() -> None:
    """Bring SheepShaver to the front so posted events reach it."""
    pid = _emulator_pid()
    if pid is None:
        return
    try:
        from AppKit import NSApplicationActivateIgnoringOtherApps, NSRunningApplication

        app = NSRunningApplication.runningApplicationWithProcessIdentifier_(pid)
        if app is not None:
            app.activateWithOptions_(NSApplicationActivateIgnoringOtherApps)
            time.sleep(0.05)
            return
    except Exception:
        pass
    try:
        subprocess.run(
            [
                "osascript",
                "-e",
                'tell application "System Events" to set frontmost of '
                "(first process whose unix id is %d) to true" % pid,
            ],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=10,
        )
    except Exception:
        pass


def check_input_works() -> None:
    """Verify synthetic events actually move the cursor (TCC check)."""
    require_modules()
    start = Quartz.CGEventGetLocation(Quartz.CGEventCreate(None))
    target = (start.x + 24.0, start.y + 24.0)
    move = Quartz.CGEventCreateMouseEvent(
        None, Quartz.kCGEventMouseMoved, target, Quartz.kCGMouseButtonLeft
    )
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, move)
    time.sleep(0.15)
    now = Quartz.CGEventGetLocation(Quartz.CGEventCreate(None))
    back = Quartz.CGEventCreateMouseEvent(
        None, Quartz.kCGEventMouseMoved, start, Quartz.kCGMouseButtonLeft
    )
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, back)
    time.sleep(0.05)
    if abs(now.x - target[0]) > 2.0 or abs(now.y - target[1]) > 2.0:
        raise HarnessError("CGEventPost has no effect -- " + TCC_MESSAGE, 2)


def _post_mouse(kind: str, gx: float, gy: float) -> None:
    """Post one mouse event (down/dragged/up/moved) at a guest coordinate."""
    require_modules()
    win = wait_window(1.0)
    host_x, host_y = guest_to_host(win, gx, gy)
    pt = (host_x, host_y)
    kinds = {
        "down": Quartz.kCGEventLeftMouseDown,
        "dragged": Quartz.kCGEventLeftMouseDragged,
        "up": Quartz.kCGEventLeftMouseUp,
        "moved": Quartz.kCGEventMouseMoved,
    }
    event = Quartz.CGEventCreateMouseEvent(
        None, kinds[kind], pt, Quartz.kCGMouseButtonLeft
    )
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, event)


def menu_select(title_xy, item_xy) -> None:
    """Choose a menu item the way a Mac expects: press on the title, drag to the
    item, release there.

    A plain click cannot do this. MenuSelect runs its own tracking loop: the
    menu opens on mouse-down and closes on mouse-up, so a down+up at the title
    opens and immediately dismisses the menu without selecting anything.
    """
    require_modules()
    activate_emulator()
    _post_mouse("moved", title_xy[0], title_xy[1])
    time.sleep(0.05)
    _post_mouse("down", title_xy[0], title_xy[1])
    time.sleep(0.30)
    # A couple of intermediate drags so the guest sees the highlight move.
    steps = 5
    for i in range(1, steps + 1):
        gx = title_xy[0] + (item_xy[0] - title_xy[0]) * i / steps
        gy = title_xy[1] + (item_xy[1] - title_xy[1]) * i / steps
        _post_mouse("dragged", gx, gy)
        time.sleep(0.06)
    time.sleep(0.20)
    _post_mouse("dragged", item_xy[0], item_xy[1])
    time.sleep(0.20)
    _post_mouse("up", item_xy[0], item_xy[1])
    time.sleep(0.20)


def click_guest(gx: float, gy: float, count: int = 1, shift: bool = False) -> None:
    require_modules()
    activate_emulator()
    win = wait_window(1.0)
    host_x, host_y = guest_to_host(win, gx, gy)
    pt = (host_x, host_y)
    flags = Quartz.kCGEventFlagMaskShift if shift else 0

    move = Quartz.CGEventCreateMouseEvent(
        None, Quartz.kCGEventMouseMoved, pt, Quartz.kCGMouseButtonLeft
    )
    if flags:
        Quartz.CGEventSetFlags(move, flags)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, move)
    time.sleep(0.05)

    for n in range(1, count + 1):
        down = Quartz.CGEventCreateMouseEvent(
            None, Quartz.kCGEventLeftMouseDown, pt, Quartz.kCGMouseButtonLeft
        )
        Quartz.CGEventSetIntegerValueField(down, Quartz.kCGMouseEventClickState, n)
        if flags:
            Quartz.CGEventSetFlags(down, flags)
        Quartz.CGEventPost(Quartz.kCGHIDEventTap, down)
        time.sleep(0.03)
        up = Quartz.CGEventCreateMouseEvent(
            None, Quartz.kCGEventLeftMouseUp, pt, Quartz.kCGMouseButtonLeft
        )
        Quartz.CGEventSetIntegerValueField(up, Quartz.kCGMouseEventClickState, n)
        if flags:
            Quartz.CGEventSetFlags(up, flags)
        Quartz.CGEventPost(Quartz.kCGHIDEventTap, up)
        time.sleep(0.04)


# US layout: character -> (virtual keycode, needs shift).
_KEYCODES = {
    "a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9,
    "b": 11, "q": 12, "w": 13, "e": 14, "r": 15, "y": 16, "t": 17,
    "1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23, "=": 24, "9": 25,
    "7": 26, "-": 27, "8": 28, "0": 29, "]": 30, "o": 31, "u": 32, "[": 33,
    "i": 34, "p": 35, "\r": 36, "l": 37, "j": 38, "'": 39, "k": 40, ";": 41,
    "\\": 42, ",": 43, "/": 44, "n": 45, "m": 46, ".": 47, "\t": 48, " ": 49,
    "`": 50,
}
_SHIFTED = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "_": "-", "+": "=", "{": "[", "}": "]",
    "|": "\\", ":": ";", '"': "'", "<": ",", ">": ".", "?": "/", "~": "`",
}
_NAMED_KEYS = {
    "return": 36, "enter": 36, "esc": 53, "escape": 53, "tab": 48, "space": 49,
    "backspace": 51, "delete": 51, "forwarddelete": 117,
    "left": 123, "right": 124, "down": 125, "up": 126,
    "home": 115, "end": 119, "pageup": 116, "pagedown": 121, "help": 114,
}


def _char_keycode(ch: str):
    if ch in _KEYCODES:
        return _KEYCODES[ch], False
    if ch in _SHIFTED:
        return _KEYCODES[_SHIFTED[ch]], True
    if ch.isupper() and ch.lower() in _KEYCODES:
        return _KEYCODES[ch.lower()], True
    raise HarnessError("cannot type character %r (unsupported by the US key map)" % ch, 2)


def _post_key(keycode: int, flags: int = 0) -> None:
    down = Quartz.CGEventCreateKeyboardEvent(None, keycode, True)
    Quartz.CGEventSetFlags(down, flags)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, down)
    time.sleep(0.02)
    up = Quartz.CGEventCreateKeyboardEvent(None, keycode, False)
    Quartz.CGEventSetFlags(up, flags)
    Quartz.CGEventPost(Quartz.kCGHIDEventTap, up)
    time.sleep(0.02)


def type_text(text: str, delay: float = 0.012) -> None:
    require_modules()
    activate_emulator()
    for ch in text:
        keycode, shift = _char_keycode(ch)
        _post_key(keycode, Quartz.kCGEventFlagMaskShift if shift else 0)
        time.sleep(delay)


def send_key(combo: str) -> None:
    require_modules()
    activate_emulator()

    parts = [p.strip() for p in combo.split("+")]
    if len(parts) > 1 and parts[0] == "":
        parts = parts[1:]  # leading "+" means the plus key

    flags = 0
    key_name = parts[-1] if parts else ""
    for mod in parts[:-1]:
        mod = mod.lower()
        if mod in ("cmd", "command"):
            flags |= Quartz.kCGEventFlagMaskCommand
        elif mod == "shift":
            flags |= Quartz.kCGEventFlagMaskShift
        elif mod in ("opt", "option", "alt"):
            flags |= Quartz.kCGEventFlagMaskAlternate
        elif mod in ("ctrl", "control"):
            flags |= Quartz.kCGEventFlagMaskControl
        else:
            raise HarnessError("unknown modifier %r in key combo %r" % (mod, combo), 2)

    lowered = key_name.lower()
    if lowered in _NAMED_KEYS:
        keycode = _NAMED_KEYS[lowered]
        shift = False
    else:
        keycode, shift = _char_keycode(key_name)
    if shift:
        flags |= Quartz.kCGEventFlagMaskShift
    _post_key(keycode, flags)


# --------------------------------------------------------------------------
# OCR and image comparison
# --------------------------------------------------------------------------


def _ocr_image(image) -> str:
    prep = image.convert("L")
    if prep.width < 1400:
        factor = 2
        prep = prep.resize((prep.width * factor, prep.height * factor), Image.LANCZOS)
    fd, path = tempfile.mkstemp(prefix="ocr-", suffix=".png", dir=os.path.join(WORK_DIR, "tmp"))
    os.close(fd)
    base = path[:-4]
    try:
        prep.save(path)
        proc = subprocess.run(
            ["tesseract", path, base, "--psm", "6", "-l", "eng"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            timeout=60,
        )
        if proc.returncode != 0:
            raise HarnessError(
                "tesseract failed: %s" % proc.stderr.decode("utf-8", "replace").strip(), 2
            )
        with open(base + ".txt", "r", encoding="utf-8", errors="replace") as f:
            return f.read().strip()
    finally:
        for p in (path, base + ".txt"):
            try:
                os.unlink(p)
            except OSError:
                pass


def ocr_region(name: str) -> str:
    return _ocr_image(capture_region(name))


def diff_fraction(current, baseline) -> float:
    if current.size != baseline.size:
        raise HarnessError(
            "size mismatch: current %s vs baseline %s -- re-record the baseline"
            % (current.size, baseline.size)
        )
    current = current.convert("RGB")
    baseline = baseline.convert("RGB")
    diff = ImageChops.difference(current, baseline).convert("L")
    hist = diff.histogram()
    changed = sum(hist[DIFF_THRESHOLD + 1:])
    total = current.width * current.height
    return changed / float(total) if total else 0.0


def _load_baseline(path: str):
    if not os.path.exists(path):
        raise HarnessError("baseline missing: %s (record it with --record first)" % path, 1)
    return Image.open(path).convert("RGB")


def save_baseline(image, path: str) -> None:
    parent = os.path.dirname(os.path.abspath(path))
    if parent:
        os.makedirs(parent, exist_ok=True)
    image.save(path)


def _record_or_assert_stable(name: str, baseline_path: str, max_fraction: float) -> None:
    current = capture_region(name)
    if RECORD:
        save_baseline(current, baseline_path)
        print("recorded %s -> %s" % (name, baseline_path))
        return
    fraction = diff_fraction(current, _load_baseline(baseline_path))
    print("stable %s: %.4f <= %.4f (%s)" % (name, fraction, max_fraction, baseline_path))
    if fraction > max_fraction:
        raise HarnessError(
            "region %r changed by %.4f (limit %.4f); OCR saw:\n%s"
            % (name, fraction, max_fraction, _ocr_image(current))
        )


def _record_or_assert_changed(name: str, baseline_path: str, min_fraction: float) -> None:
    current = capture_region(name)
    if RECORD:
        print("record mode: skipping changed-check for %s" % name)
        return
    fraction = diff_fraction(current, _load_baseline(baseline_path))
    print("changed %s: %.4f >= %.4f (%s)" % (name, fraction, min_fraction, baseline_path))
    if fraction < min_fraction:
        raise HarnessError(
            "region %r changed by only %.4f (needed %.4f); OCR saw:\n%s"
            % (name, fraction, min_fraction, _ocr_image(current))
        )


# --------------------------------------------------------------------------
# Emulator process management
# --------------------------------------------------------------------------


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def _read_pidfile():
    try:
        with open(PIDFILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def quit_emulator() -> None:
    data = _read_pidfile()
    if not data:
        return
    sup = data.get("supervisor")
    emu = data.get("emulator")

    if sup and _pid_alive(sup):
        try:
            os.kill(sup, signal.SIGTERM)
        except OSError:
            pass
        deadline = time.time() + 25
        while time.time() < deadline and _pid_alive(sup):
            time.sleep(0.2)
        if _pid_alive(sup):
            try:
                os.kill(sup, signal.SIGKILL)
            except OSError:
                pass

    if emu and _pid_alive(emu):
        try:
            os.kill(emu, signal.SIGTERM)
        except OSError:
            pass
        deadline = time.time() + 10
        while time.time() < deadline and _pid_alive(emu):
            time.sleep(0.2)
        if _pid_alive(emu):
            try:
                os.kill(emu, signal.SIGKILL)
            except OSError:
                pass

    try:
        os.unlink(PIDFILE)
    except OSError:
        pass


def _launch_args(vm_dir: str, form: str):
    if form == "config":
        return ["--config", os.path.join(vm_dir, "prefs")]
    return [vm_dir]


def _supervise(vm_dir: str, form: str) -> int:
    """Run SheepShaver as a child, clean it up on SIGTERM/exit."""
    os.makedirs(WORK_DIR, exist_ok=True)
    log = open(LOG_FILE, "ab")
    proc = subprocess.Popen(
        [SHEEPSHAVER] + _launch_args(vm_dir, form),
        stdout=log,
        stderr=log,
        stdin=subprocess.DEVNULL,
        start_new_session=True,
    )

    stopped = {"done": False}

    def _stop(*_args) -> None:
        if stopped["done"]:
            return
        stopped["done"] = True
        if proc.poll() is None:
            try:
                proc.terminate()
                proc.wait(timeout=15)
            except Exception:
                try:
                    proc.kill()
                    proc.wait(timeout=5)
                except Exception:
                    pass
        try:
            os.unlink(PIDFILE)
        except OSError:
            pass

    atexit.register(_stop)

    def _on_signal(_signum, _frame):
        sys.exit(0)

    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        try:
            signal.signal(sig, _on_signal)
        except (OSError, ValueError):
            pass

    with open(PIDFILE, "w", encoding="utf-8") as f:
        json.dump({"supervisor": os.getpid(), "emulator": proc.pid, "form": form}, f)

    try:
        return proc.wait()
    finally:
        _stop()


def start(vm_dir: str, timeout: float) -> None:
    require_modules()

    if not os.path.isfile(SHEEPSHAVER):
        raise HarnessError("SheepShaver not found at %s (set MAC_SHEEPSHAVER)" % SHEEPSHAVER, 2)
    if not os.path.isfile(os.path.join(vm_dir, "prefs")):
        raise HarnessError(
            "no prefs in %s -- run tests/emulator/vm/make_vm.sh first" % vm_dir, 2
        )
    if not _ax_trusted():
        raise HarnessError(TCC_MESSAGE, 2)

    existing = _read_pidfile()
    if existing:
        quit_emulator()

    ensure_dirs()
    try:
        os.unlink(LOG_FILE)
    except OSError:
        pass

    forms = ["bundle", "config"]
    last_error = None
    for index, form in enumerate(forms):
        budget = timeout if index == 0 else 45.0
        log = open(LOG_FILE, "ab")
        subprocess.Popen(
            [sys.executable, os.path.abspath(__file__), "_supervise", vm_dir, form],
            stdout=log,
            stderr=log,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
        log.close()

        deadline = time.time() + budget
        started_at = time.time()
        saw_pidfile = False
        win = None
        while time.time() < deadline:
            existing = _read_pidfile()
            if existing:
                saw_pidfile = True
                if not _pid_alive(existing.get("supervisor", -1)):
                    break
            elif saw_pidfile or time.time() - started_at > 3.0:
                # Supervisor never wrote a pidfile, or the emulator it
                # supervised already exited: do not sit out the whole budget.
                break
            win = find_window(WINDOW_TITLE)
            if win is not None:
                break
            time.sleep(0.5)

        if win is not None:
            activate_emulator()
            # Verify capture and input really work (TCC), and log geometry.
            image = capture(win)
            check_input_works()
            g = geometry(win)
            print(
                "SheepShaver up: window %dx%d, guest %dx%d, scale %.3f, title bar %.0fpx, "
                "launch form %s"
                % (
                    int(image.width),
                    int(image.height),
                    GUEST_W,
                    GUEST_H,
                    g["scale"],
                    g["origin_y"],
                    form,
                )
            )
            with open(os.path.join(WORK_DIR, "launch-form"), "w", encoding="utf-8") as f:
                f.write(form + "\n")
            return

        last_error = "no window"
        quit_emulator()

    tail = ""
    try:
        with open(LOG_FILE, "r", encoding="utf-8", errors="replace") as f:
            tail = "".join(f.readlines()[-20:])
    except OSError:
        pass
    raise HarnessError(
        "SheepShaver did not open a window (%s)\n--- %s ---\n%s" % (last_error, LOG_FILE, tail), 2
    )


# --------------------------------------------------------------------------
# Commands
# --------------------------------------------------------------------------


def cmd_start(args) -> int:
    vm = args.vm or os.environ.get("MAC_VM") or os.path.join(WORK_DIR, "Test.sheepvm")
    start(os.path.abspath(vm), args.timeout)
    return 0


def cmd_quit(args) -> int:
    quit_emulator()
    return 0


def cmd_wait_window(args) -> int:
    require_modules()
    win = wait_window(args.timeout, args.title or WINDOW_TITLE)
    g = geometry(win)
    print(
        "window %r: %dx%d at (%d,%d), scale %.3f"
        % (
            win["name"] or win["owner"],
            int(win["bounds"]["Width"]),
            int(win["bounds"]["Height"]),
            int(win["bounds"]["X"]),
            int(win["bounds"]["Y"]),
            g["scale"],
        )
    )
    return 0


def cmd_shot(args) -> int:
    image = capture_region(args.region)
    if args.out:
        path = os.path.abspath(args.out)
    else:
        path = os.path.join(WORK_DIR, "shots", args.name + ".png")
    save_baseline(image, path)
    print(path)
    return 0


def cmd_menu(args) -> int:
    menu_select((args.tx, args.ty), (args.ix, args.iy))
    return 0


def cmd_click(args) -> int:
    if args.name:
        gx, gy = point(args.name)
    else:
        if args.x is None or args.y is None:
            raise HarnessError("click needs X Y or --name", 2)
        gx, gy = args.x, args.y
    click_guest(gx, gy, count=2 if args.double else 1, shift=args.shift)
    return 0


def cmd_key(args) -> int:
    send_key(args.combo)
    return 0


def cmd_type(args) -> int:
    type_text(args.text)
    return 0


def cmd_ocr(args) -> int:
    region_name = args.region_opt or args.region
    print(ocr_region(region_name))
    return 0


def cmd_wait_ocr(args) -> int:
    needle = args.text.lower()
    deadline = time.time() + args.timeout
    last = ""
    while time.time() < deadline:
        last = ocr_region(args.region)
        if needle in last.lower():
            print(last)
            return 0
        time.sleep(args.interval)
    raise HarnessError(
        "OCR did not contain %r within %.0fs; last OCR:\n%s" % (args.text, args.timeout, last)
    )


def cmd_changed(args) -> int:
    _record_or_assert_changed(args.region, os.path.abspath(args.baseline), args.min_fraction)
    return 0


def cmd_stable(args) -> int:
    _record_or_assert_stable(args.region, os.path.abspath(args.baseline), args.max_fraction)
    return 0


def cmd_wait_stable(args) -> int:
    baseline = _load_baseline(os.path.abspath(args.baseline))
    deadline = time.time() + args.timeout
    last = 1.0
    while time.time() < deadline:
        last = diff_fraction(capture_region(args.region), baseline)
        if last <= args.max_fraction:
            print("stable %s: %.4f <= %.4f" % (args.region, last, args.max_fraction))
            return 0
        time.sleep(args.interval)
    raise HarnessError(
        "region %r still differs by %.4f after %.0fs (limit %.4f)"
        % (args.region, last, args.timeout, args.max_fraction)
    )


def cmd_record(args) -> int:
    save_baseline(capture_region(args.region), os.path.abspath(args.out))
    print("recorded %s -> %s" % (args.region, args.out))
    return 0


def cmd_coord(args) -> int:
    print("%d %d" % point(args.name))
    return 0


def cmd_preflight(args) -> int:
    require_modules()
    if not _ax_trusted():
        raise HarnessError(TCC_MESSAGE, 2)
    if not os.path.isfile(SHEEPSHAVER):
        raise HarnessError("SheepShaver not found at %s" % SHEEPSHAVER, 2)
    print("preflight ok: tesseract, pyobjc, Pillow, Accessibility")
    return 0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="SheepShaver test driver")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("start", help="launch the emulator and wait for its window")
    p.add_argument("--vm", help="path to the .sheepvm directory")
    p.add_argument("--timeout", type=float, default=120.0)
    p.set_defaults(func=cmd_start)

    p = sub.add_parser("quit", help="stop the emulator")
    p.set_defaults(func=cmd_quit)

    p = sub.add_parser("wait-window", help="wait for the emulator window")
    p.add_argument("--title", default=None)
    p.add_argument("--timeout", type=float, default=120.0)
    p.set_defaults(func=cmd_wait_window)

    p = sub.add_parser("shot", help="capture a region to a PNG")
    p.add_argument("name")
    p.add_argument("--region", default="full")
    p.add_argument("--out", default=None)
    p.set_defaults(func=cmd_shot)

    p = sub.add_parser("menu", help="choose a menu item: <title-x> <title-y> <item-x> <item-y>")
    p.add_argument("tx", type=int)
    p.add_argument("ty", type=int)
    p.add_argument("ix", type=int)
    p.add_argument("iy", type=int)
    p.set_defaults(func=cmd_menu)

    p = sub.add_parser("click", help="click guest coordinates")
    p.add_argument("x", type=int, nargs="?")
    p.add_argument("y", type=int, nargs="?")
    p.add_argument("--name", default=None, help="named point from coords.json")
    p.add_argument("--double", action="store_true")
    p.add_argument("--shift", action="store_true")
    p.set_defaults(func=cmd_click)

    p = sub.add_parser("key", help="send a key combo, e.g. 'cmd+,' or 'return'")
    p.add_argument("combo")
    p.set_defaults(func=cmd_key)

    p = sub.add_parser("type", help="type ASCII text")
    p.add_argument("text")
    p.set_defaults(func=cmd_type)

    p = sub.add_parser("ocr", help="OCR a region")
    p.add_argument("region", nargs="?", default="full",
                   help="named region from coords.json (default: full)")
    p.add_argument("--region", dest="region_opt", default=None)
    p.set_defaults(func=cmd_ocr)

    p = sub.add_parser("wait-ocr", help="wait until a region's OCR contains text")
    p.add_argument("text")
    p.add_argument("--region", default="full")
    p.add_argument("--timeout", type=float, default=120.0)
    p.add_argument("--interval", type=float, default=1.0)
    p.set_defaults(func=cmd_wait_ocr)

    p = sub.add_parser("changed", help="assert a region differs from a baseline")
    p.add_argument("region")
    p.add_argument("baseline")
    p.add_argument("min_fraction", type=float)
    p.set_defaults(func=cmd_changed)

    p = sub.add_parser("stable", help="assert a region matches a baseline")
    p.add_argument("region")
    p.add_argument("baseline")
    p.add_argument("max_fraction", type=float)
    p.set_defaults(func=cmd_stable)

    p = sub.add_parser("wait-stable", help="poll until a region matches a baseline")
    p.add_argument("region")
    p.add_argument("baseline")
    p.add_argument("max_fraction", type=float)
    p.add_argument("--timeout", type=float, default=10.0)
    p.add_argument("--interval", type=float, default=0.5)
    p.set_defaults(func=cmd_wait_stable)

    p = sub.add_parser("record", help="write a region's current frame as a baseline")
    p.add_argument("region")
    p.add_argument("out")
    p.set_defaults(func=cmd_record)

    p = sub.add_parser("coord", help="print a named point from coords.json")
    p.add_argument("name")
    p.set_defaults(func=cmd_coord)

    p = sub.add_parser("preflight", help="check tesseract, modules, TCC permission")
    p.set_defaults(func=cmd_preflight)

    p = sub.add_parser("_supervise", help=argparse.SUPPRESS)
    p.add_argument("vm")
    p.add_argument("form", choices=["bundle", "config"])
    p.set_defaults(func=lambda a: _supervise(a.vm, a.form))

    return parser


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    ensure_dirs()
    try:
        return int(args.func(args) or 0)
    except HarnessError as exc:
        prefix = "FAIL: " if exc.code == 1 else "error: "
        print(prefix + str(exc), file=sys.stderr)
        return exc.code
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
