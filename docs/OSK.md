# On-screen keyboard — design notes

This fork adds a touch on-screen keyboard to ravindu644/recovery-console.
Source files specific to the OSK:

- `include/osk.h` — public API, `OSK` struct, `OskKey` struct.
- `osk.c` — layout tables, hit-test, render, long-press repeat.
- `display.c` / `include/display.h` — `display_draw_rect`,
  `display_draw_text`, plus the OSK-specific `display_draw_text_osk`
  / `display_text_width_osk` (larger font face for legible labels)
  and `display_draw_rounded_rect` (used as a sharp rect at radius=0
  in current build, but kept available).
- `font.c` / `include/font.h` — adds a second `FT_Face` at a larger
  pixel size (`font_init_osk(40)`) with its own glyph cache, so OSK
  labels render bigger than the terminal grid without affecting it.
- `main.c` — drops the `EV_KEY`-only filter on the input loop, tracks
  `ABS_MT_POSITION_X/Y` per slot, dispatches key presses on
  `BTN_TOUCH=0`, and pumps `osk_pump_repeat()` per loop iteration so
  long-pressed arrows / backspace auto-repeat.

## Layout

Two pages. Page 0 (letters) has the extras row (terminal-control +
arrows + home/end) on top. Page 1 (numbers + symbols) replaces the
extras row with more symbols so that **every printable ASCII
character** is reachable in one tap.

```
PAGE 0 (alpha)
┌─────────────────────────────────────────────┐
│ esc tab ctrl alt  ↑   ↓   ←   →  home end   │  extras (repeatable arrows)
│  q   w   e   r   t   y   u   i   o   p      │
│   a   s   d   f   g   h   j   k   l         │  9 keys, naturally wider
│ ⇧   z   x   c   v   b   n   m  ⌫            │  shift 1.5×, bsp 1.5×
│ ?123    ,        space         .    ⏎       │  toggle 1.5×, space 5×
└─────────────────────────────────────────────┘

PAGE 1 (numbers + ALL symbols)
┌─────────────────────────────────────────────┐
│  1   2   3   4   5   6   7   8   9   0      │
│  @   #   $   %   &   *   -   +   (   )      │
│  "   '   :   ;   !   ?   =   /   [   ]      │
│  `   ~   ^   <   >   _   |   \   {   }   ⌫  │  11 cells incl. bsp
│ ABC     ,        space         .    ⏎       │
└─────────────────────────────────────────────┘
```

## Sizing model

`OskKey.span` is in **half-cells**: a 1-cell letter key has `span=2`,
shift/backspace/page-toggle/enter have `span=3` (1.5 cells), the
spacebar has `span=10` (5 cells). Each row's pixel layout is
computed independently:

```
key_pixel_width = key.span * screen_w / row_total_span
```

So a 9-key row (e.g. a-l on page 0, total span 18) renders with keys
slightly wider than a 10-key row (total 20), and the 11-key page-1
row 3 (total 22) is slightly tighter — natural Gboard behaviour
without manually computing margins.

`row_total_span[OSK_ROWS]` is recomputed on every page toggle.

## OskKey struct

```c
typedef struct {
  uint16_t code;       /* KEY_* from <linux/input-event-codes.h> */
  const char *label;   /* what to draw on the cap (UTF-8 OK now) */
  uint8_t span;        /* width in half-cells */
  bool autoshift;      /* implicit SHIFT for shifted-symbol caps */
  bool repeatable;     /* long-press auto-repeats (arrows, bsp) */
} OskKey;
```

`autoshift` lets caps labelled `{` / `:` / `?` etc. dispatch the
underlying unshifted key code with a synthetic SHIFT wrapped around
it. Same code path as sticky shift, just one-shot.

`repeatable` flags keys whose long-press should auto-repeat (currently
the four arrows on the extras row plus both backspace caps). The
press timestamp is captured at touch-down; `osk_pump_repeat()` fires
the first repeat at 400 ms, subsequent at 60 ms intervals.

## Touch flow

```
EV_ABS ABS_MT_POSITION_X / Y  → update touch_x / touch_y
                              → if finger_down, re-run osk_touch_press
                                so highlight tracks finger live
EV_KEY BTN_TOUCH = 1          → arm finger_down
                              → reset touch_x = touch_y = -1 and
                                osk.pressed_* = -1 so we don't render
                                stale-cell highlight from previous tap
                              → ABS handler above will fire press once
                                fresh coords land
EV_KEY BTN_TOUCH = 0          → osk_touch_release returns KEY_*
                              → dispatch with synthetic shift / ctrl /
                                alt around it as needed
                              → clear sticky state
```

The "BTN_TOUCH before position" race is real on dre — the Novatek
nt36672c driver fires `BTN_TOUCH=1` before the new finger's
`ABS_MT_POSITION_*` events within a SYN report, so the press-time
hit test can land on yesterday's coordinates. Solution: defer the
press until ABS arrives, then re-run hit-test on every position
event so dragging across keys updates the highlight live.

## Sticky modifier behaviour

`osk_touch_release()` traps modifier-key codes and toggles the
matching `OSK` field:

```c
case KEY_LEFTSHIFT:  k->shift = !k->shift; return 0;
case KEY_LEFTCTRL:   k->ctrl  = !k->ctrl;  return 0;
case KEY_LEFTALT:    k->alt   = !k->alt;   return 0;
case KEY_PAGE_TOGGLE:k->page  = (k->page+1) % 2; recompute_spans(k); return 0;
default:             return code;
```

`KEY_PAGE_TOGGLE` is `0xFFF1` — outside the Linux `KEY_*` range so
`input_ev_to_pty` won't accidentally interpret it.

`main.c` reads the latches at dispatch time and wraps the
keystroke with synthetic SHIFT / CTRL / ALT press+release so the
existing `input_ev_to_pty` char-mapping logic applies unchanged.

## Long-press repeat

```c
uint16_t osk_pump_repeat(OSK *k, uint64_t now_ms);
```

Called every main-loop iteration. Returns the `KEY_*` to inject
when a held cap (with `repeatable: true`) has passed the initial
delay (`OSK_REPEAT_DELAY_MS = 400`) and the per-repeat interval
(`OSK_REPEAT_INTERVAL_MS = 60`) has elapsed since the last fire.
Returns 0 otherwise. Currently flagged repeatable: `KEY_UP`,
`KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`, both `KEY_BACKSPACE` caps.

## Visual feedback

Palette (Gboard-ish dark):

```
bg            #1f1f23  (very dark, slightly cool)
cap (letter)  #3a3a42  (medium grey)
cap (mod)     #2c2c34  (darker — modifiers, space, digits row)
cap pressed   #56565e  (lighter feedback while finger is down)
cap latched   #3a6e98  (blue accent for sticky shift/ctrl/alt)
top hilite    #48484e  (1-px lighter line on cap top edge)
label         #e8e8ee  (off-white)
```

Sharp caps in current build (`OSK_RADIUS_PX = 0`); the radius
constant + `display_draw_rounded_rect` primitive are kept around
so re-enabling rounding is a one-line change.

## Larger label font

`font_init_osk(40)` opens a second `FT_Face` at 40-px pixel size
(vs. terminal's `FONT_SIZE = 22`). The OSK uses
`display_draw_text_osk` / `display_text_width_osk` which dispatch
through that face and its dedicated glyph cache. The terminal grid
is unaffected.

## Shift-uppercase labels

When `k->shift` is true and the cap is a single ASCII letter
(`KEY_A..KEY_Z` with `'a'..'z'` label), `osk_render` substitutes the
uppercase form before drawing. So pressing `⇧` flips the displayed
letters to caps without rebuilding the layout table.

## Entry points

```c
bool     osk_init         (OSK *k, int screen_w, int screen_h);
int      osk_height_for   (int screen_w, int screen_h);
uint16_t osk_hit_test     (const OSK *k, int x, int y, int *r, int *c);
uint16_t osk_touch_press  (OSK *k, int x, int y, uint64_t now_ms);
uint16_t osk_touch_release(OSK *k, int x, int y);
uint16_t osk_pump_repeat  (OSK *k, uint64_t now_ms);
void     osk_render       (DisplayDev *d, OSK *k);
```

## Build

```
make aarch64
```

Static aarch64-musl binary at `output/recovery-console-aarch64`.
Drop into `~/fromrecovery/ramdisk-overlay/system/bin/recovery-console`
and rebuild boot images.

## Touch wake — why main.c writes to /proc/touchpanel/force_resume

dre's Novatek touch IC enters suspend when the panel notifier
chain reports panel-off. It only resumes on a real
`mdss_panel_notifier` event. `display_init` in this binary takes the
legacy `DRM_IOCTL_MODE_SETCRTC` path because:

- `drm.c` only sets `DRM_CLIENT_CAP_UNIVERSAL_PLANES`, not
  `DRM_CLIENT_CAP_ATOMIC`. Without the atomic cap, the kernel
  filters out the `ACTIVE` and `MODE_ID` properties from
  `OBJ_GETPROPERTIES`, so `atomic_modeset()` finds prop IDs of 0
  and short-circuits to legacy.
- We tried setting `DRM_CLIENT_CAP_ATOMIC = 1` in this session.
  The build flashed cleanly but the panel went black on boot —
  recovery-console started, claimed DRM, but nothing rendered.
  Reverted. We didn't fully diagnose the post-cap path; if you
  retry it, expect to debug the atomic commit ioctl + buffer
  attachments next.

The legacy path doesn't fire the panel notifier the touch IC
listens for, so the chip stays in suspend and zero events come out
of `/dev/input/event1`.

Fix: a kernel patch in `dre-droidspaces-kernel`
(`patches/droidspaces/003-touch-force-resume.patch`) adds a
`/proc/touchpanel/force_resume` write hook that calls the driver's
`lcd_on_event` directly. recovery-console's `main.c` writes "1" to
that node once after `display_init`. The touch chip wakes within a
few hundred milliseconds.

If the kernel patch isn't applied, `main.c` logs
`touchpanel force_resume node missing — touch will be dead` and
the OSK renders fine but no touches reach it.

## UTF-8 in labels

`display_draw_text` (and its OSK variant) decode UTF-8 properly now
— earlier they substituted `0xFFFD` for any byte ≥ 0x80, which
turned every non-ASCII label into a fallback glyph. Arrows and
similar codepoints (↑↓←→⇧⌫⏎) render via FreeType against
`font.ttf`. If a glyph is missing from the font, the chosen label
renders as a tofu-box; pick a different codepoint or fall back to
ASCII (`Up`, `Dn`, etc.) for that cap.
