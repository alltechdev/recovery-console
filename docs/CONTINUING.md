# Continuing the work — recovery-console

A runbook for continuing development on this fork.

## Read first

- `docs/OSK.md` — the on-screen keyboard's design and code paths.
- Upstream `README.md` — the unmodified recovery-console docs from
  ravindu644. Useful for the term emulator, DRM, libseat-less VT
  handling parts that we haven't changed.
- `dre-droidspaces-recovery/docs/INIT-FLOW.md` — how this binary
  gets launched and what it's doing inside the device boot path.

## Repo state

- Forked from `ravindu644/recovery-console`. `origin` points at our
  fork (`alltechdev/recovery-console`); `upstream` points at
  ravindu's original. `git fetch upstream main` pulls his changes
  for cherry-picking.
- `main` is at `f958b6a` "osk: fix stale-cell flash on each new tap"
  as of the last session. Two fork-specific commits live on top of
  upstream's tree:
  - `47aec2e` — termux-style extras row + numbers/symbols page
    (WIP, POC label).
  - `f958b6a` — fixes the "highlights previous key on new tap"
    race by deferring the press render until ABS arrives.
- Release `v20260426` attaches the static aarch64 binary. Pairs
  with `dre-droidspaces-kernel` `v20260426` (which has the
  `force_resume` proc node we write to).

## Build

```
make aarch64
```

Output at `output/recovery-console-aarch64`. Static aarch64-musl,
~936K. The Makefile finds the cross-compiler via
`MUSL_CROSS=<dir>` env, `$HOME/toolchains/aarch64-linux-musl-cross/`,
or `aarch64-linux-musl-gcc` on `PATH` — first hit wins.

If the cross-compiler is missing:

```
mkdir -p $HOME/toolchains && cd $HOME/toolchains
curl -fsSL -O https://musl.cc/aarch64-linux-musl-cross.tgz
tar xf aarch64-linux-musl-cross.tgz
```

## Edit-build-test cycle

```
# In ~/recovery-console:
vim osk.c                                                  # or whatever
make aarch64

# Stage into the recovery overlay:
cp output/recovery-console-aarch64 \
   ~/fromrecovery/ramdisk-overlay/system/bin/recovery-console

# Rebuild boot images:
cd ~/fromrecovery && ./build.sh

# Flash + boot:
adb reboot bootloader && sleep 5
fastboot flash vendor_boot_b out/vendor_boot-slot-b.img
fastboot flash boot_b out/boot-slot-b.img
fastboot reboot recovery
```

`fastboot reboot recovery` — never bare `fastboot reboot`. Slot B
has no Android. [memory: feedback_recovery_reboot.md]

## Pulling upstream changes

ravindu644 ships occasional fixes upstream. Periodically:

```
git fetch upstream
git log --oneline main..upstream/main          # see what's new
git cherry-pick <commit>                       # bring over the ones we want
```

The OSK layer (`include/osk.h`, `osk.c`, edits to `main.c` /
`display.{c,h}` / `Makefile`) is contained — most upstream commits
will merge cleanly. Watch for changes to `main.c`'s input loop or
to `display_render`'s call sites; those are the files where we
inject OSK calls.

## Open work

### Layout polish (low effort)

- The symbol page is missing direct caps for backtick, double-quote,
  question mark, plus-sign. Some are present via autoshift but
  positioning is opinionated. Suggested layout review session: lay
  out a Termux symbol page table and copy our caps to match.
- HOME/END caps on the extras bar feel cramped at 8 keys per row.
  Consider dropping HOME/END or replacing with PGUP/PGDN since
  shells already have arrow-key alternates.

Touch ergonomics: keys are 108×160 px. Easy to mis-tap edge cells.
Could add 1–2 px hit-test slop or collapse into 9-cells per row
for fewer, wider caps.

### Long-press / shift on long-tap (medium effort)

`p0_row3` doesn't have shift access on the alpha page; tapping
shft+letter requires a multi-step. Long-press detection would let
a long-press of a letter type the uppercase variant. Implementation:

- In `main.c`, time the press duration. If >300 ms before BTN_TOUCH=0,
  treat as long-press: dispatch with `KEY_LEFTSHIFT` even without
  the sticky shift latch.
- Optionally show the uppercase glyph as a "popup" cap over the
  finger during the press window. Means a small extra render path.

### Drag-to-cancel + slide-to-different-key (low effort)

Already half-implemented: `osk_touch_release` returns 0 if the
release coords land outside the OSK area or in a different cell.
But sliding into a NEW cell currently dispatches the new cell's
key. Match standard mobile-keyboard behaviour: only dispatch if
release is on the SAME cell as press; otherwise cancel. Need to
record press cell separately from current pressed_row/_col and
compare on release.

### Upstream a Wayland-aware build flag (large effort)

Right now the binary drives DRM directly. If the user ever wants
to run alongside a wayland compositor (phoc on tty7 etc.), we'd
need a "guest mode" where we render to a Wayland surface instead
of `/dev/dri/card0`. wlroots has the right pieces. Out of scope
for now.

## Common gotchas

- **Touch silent on a fresh boot**: confirm the `force_resume`
  proc node exists (`adb shell ls /proc/touchpanel/force_resume`).
  If missing, the kernel doesn't have patch 003 applied. Rebuild
  the kernel from `~/dre`. `recovery-console` itself logs
  `touchpanel force_resume sent` or
  `touchpanel force_resume node missing` at startup; these messages
  go to stderr which is currently `/dev/null` for the init service,
  so check `/tmp/recovery.log` (lineage's combined recovery log) or
  rebuild with stderr→a file.
- **Press visual lags on first tap after launch**: known. The
  initial `touch_x = touch_y = -1` from `osk_init` means the very
  first ABS event is needed before the first press registers. Not
  visually disruptive; the press updates as soon as the finger
  lands.
- **Modifier latch sticking after a slide-cancel**: today, sliding
  off the OSK area cancels the dispatch but does NOT clear the
  sticky CTRL/ALT/SHIFT latches. So a tap+slide-off, then a normal
  tap, will accidentally fire with the latch active. Fix: clear
  modifiers in `osk_touch_release` if it returns 0 because of an
  off-cell miss (not just because the cap was a modifier). One-line
  edit.

## What NOT to do

- **Don't add features that depend on libseat / VT.** This binary
  is currently used in a kernel that doesn't have CONFIG_VT and
  drives DRM directly. Anything that opens `/dev/tty0` will fail.
  See `dre-droidspaces-recovery/docs/WAYLAND.md` for the longer
  story.
- **Don't add `DRM_CLIENT_CAP_ATOMIC=1` without a separate test
  cycle.** Tried in this session — panel went black, reverted.
  See `docs/OSK.md` "Touch wake" for the note. If you DO retry it,
  expect to debug the atomic commit ioctl + the dumb-buffer
  binding next.
