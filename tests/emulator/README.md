# Emulator test harness (SheepShaver, Mac OS 9)

Plan tier **T2** (boot, launch, render, menus, dialogs, response paths) and the
optional tier **T3** (a real HTTPS request to `api.anthropic.com`) for
`macintosh-ai`.

CI cannot run this: the ubuntu runners have no Mac, and the ROM and Mac OS
volume cannot be committed. T2/T3 run **locally on this Mac**, against a
disposable APFS clone of the installed Mac OS 9 volume. Nothing here writes to
the user's real VM or outside `tests/emulator/.work/`.

## What is on this machine

Verified while writing this harness:

| thing | path / value |
|---|---|
| SheepShaver binary | `/Applications/Mac OS 9.app/Contents/Resources/Files/SheepShaver.app/Contents/MacOS/SheepShaver` (universal, arm64 + x86_64) |
| VM prefs (used as source) | `~/Library/Application Support/MacOS9/MacOS.sheepvm/prefs` |
| boot volume (real) | `/Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm/MacOS9.sparsebundle` (3.8 GB, `Apple_HFS`) |
| ROM (real) | `.../Files/MacOS.sheepvm/Mac OS ROM` (1.9 MB) |
| guest prefs | `ether slirp`, `screen win/1024/768`, `ramsize 1073741824`, `jit true`, `jit68k false` |
| tesseract | `/opt/homebrew/bin/tesseract` 5.5.4 |
| python | `/opt/homebrew/bin/python3` 3.15.2 |

Two environment facts contradict the plan text, so the scripts do not hard-code
either:

1. **The 4.2 GB volume is not under `~/Library/Application Support/MacOS9/`.**
   That directory holds only the `.sheepvm` (prefs, ROM, keycodes, nvram); its
   `prefs` points `disk` at the sparsebundle inside the app bundle. `make_vm.sh`
   therefore reads the `disk` and `rom` entries from whichever `prefs` it finds
   (`MAC_VM_DIR` overrides), and resolves relative paths against that directory.
2. **pyobjc-framework-Quartz and Pillow were not installed** in the Homebrew
   python when this harness was written (`import Quartz` failed). Install them
   as below. Also note Python 3.15 removed `os.setxattr`, so `make_vm.sh` uses
   `ctypes`/`xattr` to move the resource fork onto HFS.

## One-time setup

```sh
brew install tesseract
/opt/homebrew/bin/python3 -m pip install -r tests/emulator/requirements.txt
```

If Homebrew's Python refuses the install (PEP 668), use a venv; `run.sh` picks
it up automatically:

```sh
/opt/homebrew/bin/python3 -m venv tests/emulator/.venv
tests/emulator/.venv/bin/python3 -m pip install -r tests/emulator/requirements.txt
```

`requirements.txt` pins pyobjc-framework-Quartz 10.3.1 only for Python < 3.14:
that release has no build for the Homebrew Python 3.15 on this machine (and it
still imports `pkg_resources`). For 3.14+ the marker selects pyobjc 13.x, which
was verified here with Pillow 11.3.0 in `tests/emulator/.venv`.

Then grant **Accessibility** and **Screen Recording** to the terminal (or
whatever process runs the harness) in System Settings > Privacy & Security.
Without them `CGEventPost` is silently dropped and `CGWindowListCreateImage`
returns nil. The harness detects this and exits **2** with
`grant Accessibility + Screen Recording to this process`; approve it once in
the GUI, there is no command-line equivalent.

You also need `make build` to have produced `dist/MacintoshAI.APPL` and
`dist/%MacintoshAI.ad` (the AppleDouble companion carrying the resource fork —
Retro68 in the Linux build container writes the data fork only).

## Running

```sh
tests/emulator/run.sh 01-launch --record    # first run only: writes baselines/launch.png
tests/emulator/run.sh 01-launch
tests/emulator/run.sh 02-settings
tests/emulator/run.sh 03-no-key
tests/emulator/run.sh 04-live               # self-skips without ANTHROPIC_API_KEY
ANTHROPIC_API_KEY=... tests/emulator/run.sh 04-live
```

`run.sh` does not build anything. Each invocation:

1. preflights python modules, tesseract, Accessibility, and `dist/`;
2. APFS-clones the boot volume to `.work/MacOS9Test.sparsebundle`
   (`cp -cR`, instant and space-free; it refuses a 4 GB copy on non-APFS);
3. merges `dist/MacintoshAI.APPL` + `dist/%MacintoshAI.ad` (AppleDouble: data
   fork, resource fork, type/creator) and `ditto`s it into
   `System Folder:Startup Items:MacintoshAI` on the clone, then verifies the
   resource fork and `APPL`/`McAI` made it to HFS (verified here against the
   real volume: clone + install + detach takes ~9 s);
4. renders `.work/Test.sheepvm/prefs` from `vm/prefs.tmpl` (clone + ROM +
   per-run `extfs` share dir), boots SheepShaver, waits for its window;
5. runs the scenario, then quits SheepShaver.

Exit codes: **0** pass, **1** assertion failed (the diff fraction and the OCR
text it saw are printed), **2** environment missing (no `dist/`, no tesseract,
no python modules, no SheepShaver, no TCC permission, clone/attach failure).

`--keep` keeps `.work/` (screenshots in `.work/shots`, emulator stdout/stderr
in `.work/sheepshaver.log`). A failed scenario keeps it automatically. A pass
removes it.

### SheepShaver invocation form

SheepShaver accepts a `.sheepvm` directory as its first argument (both
`.sheepvm` and `--config` appear in the binary's strings) and `--config <prefs>`
as the older form. `driver.py start` probes the `.sheepvm` bundle form first and
falls back to `--config .work/Test.sheepvm/prefs`; the winner is written to
`.work/launch-form` and printed as `launch form bundle|config`.

This harness was built without booting the emulator (the integration owner runs
the scenarios after the app builds), so the winning form on this machine will be
recorded by the first `run.sh` invocation — check `.work/launch-form`. The
binary is launched in place from inside `Mac OS 9.app`, so its
`@rpath/SDL2.framework` resolves normally.

Note that `hdiutil attach` is *not* usable for the cloned volume here: it asks
"There may be a problem with this disk image. Are you sure you want to open
it?" (the sparsebundle has no checksum) and cancels without a terminal.
`make_vm.sh` therefore uses `diskutil image attach --mountOptions nobrowse`
first, with `hdiutil -noverify` only as a fallback, and detaches with
`diskutil unmount force`. This was verified against the real volume.

## Scenarios

Each file is one assertion chain; `01-launch` is the only one that needs
`--record` first.

| scenario | proves |
|---|---|
| `01-launch.sh` | the Startup Items install (resource fork + type/creator) survived HFS, the 68k app launches under Mac OS 9, the splash says "AI"/"Start Chat", and the full frame is stable (≤2% vs `baselines/launch.png`). |
| `02-settings.sh` | `Cmd-,` opens `DLOG 140`; OCR sees `Model:` and `API key:`; the screen differs from the launch frame by >5%; Esc or Cancel closes it and the splash frame returns (≤2%). |
| `03-no-key.sh` | Start Chat opens the chat window; typing and Return submit; without a key the transcript reports `API key` within 30 s; typing afterwards still changes the input region (not hung). |
| `04-live.sh` | with `ANTHROPIC_API_KEY`: enter the key in settings, send `Reply with exactly: PONG`, see `PONG` within 120 s, print send→PONG elapsed time. Without the key: prints `SKIP` and exits 0. |

Reference frames live in `baselines/` (only `launch.png` is needed; it is
created by `01-launch --record`). Baselines are host-window captures of the
guest screen, so re-record after changing the guest resolution, the SheepShaver
window scale, or the splash layout.

## Driver

`driver.py` is the only thing that talks to the emulator. It is also usable by
hand against a running VM:

```sh
tests/emulator/driver.py start --vm tests/emulator/.work/Test.sheepvm
tests/emulator/driver.py shot full --out /tmp/frame.png
tests/emulator/driver.py click 512 318
tests/emulator/driver.py key "cmd+,"
tests/emulator/driver.py type "hello"
tests/emulator/driver.py ocr full
tests/emulator/driver.py changed full baselines/launch.png 0.05
tests/emulator/driver.py quit
```

Commands: `start`, `quit`, `wait-window`, `shot`, `click`, `key`, `type`,
`ocr`, `wait-ocr`, `changed`, `stable`, `wait-stable`, `record`, `coord`,
`preflight`.

* **Capture** is `CGWindowListCreateImage` on the SheepShaver window; guest
  coordinates are converted through the window geometry each time (scale +
  title bar are derived from the window bounds, so host display size does not
  matter). The guest must be 1024×768, matching `screen win/1024/768`.
* **Input** is `CGEventPost`; keyboard events use US virtual keycodes and
  modifiers, mouse events use guest coordinates. SheepShaver is activated
  before input so clicks land in the emulator.
* **OCR** is tesseract `--psm 6`, with a 2× upscale for small regions.
* **Diffs** count pixels whose luminance differs by more than 24, as a fraction
  of the region.
* **Cleanup**: `driver.py _supervise` runs SheepShaver as a child, registers
  `atexit` + `SIGTERM`/`SIGINT`/`SIGHUP` handlers that terminate it, and
  `run.sh` calls `driver.py quit` on every exit path. No guest disk is mounted
  by the driver at any point (only `make_vm.sh` mounts, and it detaches).

## Coordinates

`scenarios/coords.json` holds regions `[x, y, w, h]` and points `[x, y]` in
guest pixels. They were derived from the layout in `src/ui/splash_window.c`
(768×576 window centred at 128,96; 120×25 button at `portRect.top+190`),
`src/ui/chat_window.c` (819×688 window at 102,40; input strip above the bottom
margin) and `DLOG 140` in `src/main.r` (400×180 at 60,80).

To re-derive after a UI change:

```sh
tests/emulator/run.sh 01-launch --record --keep
open tests/emulator/.work/shots/launch.png      # read pixel positions
# or, for word boxes:
tesseract tests/emulator/.work/shots/launch.png - --psm 6 tsv | column -t
```

Point values are centre-of-control; the scenarios only need the click to land
inside the control or text field. If `02-settings` stops finding `Model:`, the
dialog probably moved (the settings `DLOG` rect or a `CenterWindow` call) —
re-derive `settings_*` the same way.

## Notes and limits

- The APFS clone is modified read-write and discarded; the real
  `MacOS9.sparsebundle` is only read by `cp -cR`. If the source volume is HFS
  **standard** rather than HFS+, recent macOS may only mount it read-only and
  `make_vm.sh` fails at the Startup Items install; this volume reports
  `Apple_HFS` and mounted read-write in testing.
- The harness removes `MacintoshAI Prefs` from the clone before boot, so every
  run starts with an empty API key and default provider. The key typed by
  `04-live` is written only into the clone.
- `extfs` points at `.work/share`, never `~/Documents`, so a scenario cannot
  read or write user files through the guest.
- OCR of 12 pt Geneva is imperfect; the assertions use short substrings
  (`AI`, `Model:`, `API key:`, `PONG`) and print the full OCR text and the diff
  fraction on failure. That text is the evidence — do not re-run to "confirm".
- `04-live` assumes the dialog's key field is empty (true for a fresh clone)
  and that `Cmd-,`/Return reach the app; a certificate failure shows up as
  `-9984` / `certificate not trusted` in the transcript and must not be worked
  around by weakening verification.
- Basilisk II is **not** supported by this harness: there is no 68k ROM on this
  machine, so any Basilisk path would exit 2. T2/T3 run under SheepShaver.

## Environment notes found while recording the baselines

These are facts about this machine, not about the app; they are recorded so the
next person does not rediscover them.

- The boot volume is `/Applications/Mac OS 9.app/Contents/Resources/Files/MacOS.sheepvm/MacOS9.sparsebundle`;
  `~/Library/Application Support/MacOS9/MacOS.sheepvm/` holds only the ROM and
  the prefs. `make_vm.sh` follows the `disk` and `rom` lines from the prefs.
- Attaching the clone read-write makes DiskImages rewrite the bundle's
  `Info.plist` to `bundle-backingstore-version 2`. SheepShaver only reads
  version 1 and refuses the volume with `sparsebundle: Bad version`, so
  `make_vm.sh` snapshots the plist after cloning and restores it after
  detaching. Without that, SheepShaver boots to a blank screen and every
  scenario fails on OCR.
- `hdiutil attach` prompts on this OS and cancels without a TTY; the harness
  uses `diskutil image attach --mountOptions nobrowse`.
- ditto does not turn an `._name` AppleDouble into a resource fork for a single
  file. The app is staged as a real file (data fork + `com.apple.ResourceFork` +
  `com.apple.FinderInfo` xattrs) and then copied onto the volume.
- The guest's desktop has an alias to `Unix:SendToMacOS9`; the extfs share
  therefore contains a `SendToMacOS9/` directory, or the Finder raises "File …
  wasn't found" over the desktop at every boot.
- A 2 MB application partition makes Mac OS 9 drop the Startup Item without an
  alert. The app requests 3 MB (see README, Requirements).
- `CGWindowListCreateImage` returns the SheepShaver window with its title bar
  when the window is key and without it otherwise, so `driver.py` derives the
  scale and the title-bar offset from the image it actually got instead of
  assuming 28 px.

## Open issue: menu tracking under SheepShaver

`02-settings` and `03-no-key` are not green on this machine, and the reason is
in the harness, not (as far as I could establish) in the app:

- A plain click cannot operate a Mac menu. `MenuSelect` runs its own tracking
  loop: the menu opens on mouse-down and closes on mouse-up, so a down+up on
  the title opens and immediately dismisses the menu without selecting an item.
  `driver.py` now has a `menu <title-x> <title-y> <item-x> <item-y>` command
  that presses on the title, drags to the item and releases there.
- With that, the guest leaves the menu title highlighted and never draws the
  drop-down, i.e. `MenuSelect` appears to be waiting for a mouse-up that
  `CGEventPost` already delivered. Whatever is dropped there, the result is
  that no application menu can be driven from this harness yet.
- Cmd-, reaches the app as a key event but `MenuKey` does not match it, so the
  keyboard equivalent is not a workaround either.

What is nevertheless established: the splash renders (01-launch, recorded and
re-run at 0.0000 difference), the app brings itself to the front on launch, its
menu bar shows File/Models/Extras/Help, and the settings dialog has been seen
drawn with the persisted model in its field.

Fixing this needs a mouse-up that the guest's MenuSelect actually consumes —
likely a `CGEventPost` at `kCGSessionEventTap` instead of `kCGHIDEventTap`, or
`ToScreenCoordinates`-consistent dragging. Recorded here rather than guessed at.
