# On-screen keyboard — design notes

This fork adds a touch on-screen keyboard to ravindu644/recovery-console.
Source files specific to the OSK:

- `include/osk.h` — public API, `OSK` struct, `OskKey` struct.
- `osk.c` — layout tables, hit-test, render.
- Edits in `display.c` and `include/display.h` — exposes
  `display_draw_rect`, `display_draw_text`, `display_text_width`,
  `display_get_width`, `display_get_height` so `osk.c` doesn't need
  to reach into the file-static framebuffer globals.
- Edits in `main.c` — drops the `EV_KEY`-only filter on the input
  loop, tracks `ABS_MT_POSITION_X/Y` per slot, dispatches key
  presses on `BTN_TOUCH=0`.
- Edits in `Makefile` — adds `osk.c` to `SRCS` and `osk.h` /
  `input.h` to header deps.

## Layout

5 rows of 10 cell-units. `OSK_HEIGHT_PX = 800` (configurable in
`osk.c`); each row is `OSK_HEIGHT_PX / OSK_ROWS = 160 px`. Cell
width is `screen_w / 10 = 108 px` on dre's 1080×2400 panel.

```
                    ┌─────────────────────────────────────────────┐
                    │ ESC TAB CTRL ALT  ^   v   <   >   Hm  End   │  (extras, both pages)
                    ├─────────────────────────────────────────────┤
PAGE 0 (alpha)      │  q   w   e   r   t   y   u   i   o   p     │
                    │  a   s   d   f   g   h   j   k   l   <x    │
                    │ shft z   x   c   v   b   n   m   .   ent   │
                    │ ?123 ,   ___space___   -   /                │
                    └─────────────────────────────────────────────┘

                    ┌─────────────────────────────────────────────┐
                    │ ESC TAB CTRL ALT  ^   v   <   >   Hm  End   │
                    ├─────────────────────────────────────────────┤
PAGE 1 (sym)        │  1   2   3   4   5   6   7   8   9   0     │
                    │  [   ]   {   }   \   |   ;   :   =   <x    │
                    │ shft _   $   %   ^   &   *   ~   .   ent   │
                    │ ABC  ,   ___space___   -   /                │
                    └─────────────────────────────────────────────┘
```

The bottom row's `space` cap has `span = 6`, fillers `,` `-` `/`
take a single cell each, leaving 10 cell-units used.

## OskKey struct

```c
typedef struct {
  uint16_t code;      /* KEY_* from <linux/input-event-codes.h> */
  const char *label;  /* drawn on the cap */
  uint8_t span;       /* width in cell units */
  bool autoshift;     /* implicit SHIFT for the dispatch */
} OskKey;
```

`autoshift` is the trick that lets caps labelled `{` map to
`KEY_LEFTBRACE` (which on its own types `[`) — main.c sees
`osk.last_autoshift` after a release and synthesizes a
`KEY_LEFTSHIFT` press+release wrapping the dispatch. Same code path
as sticky shift, just one-shot.

## Touch flow

`main.c` event handling for the touchpanel:

```
EV_ABS ABS_MT_POSITION_X / Y  → update touch_x / touch_y
                              → if finger_down, re-run osk_touch_press
                                so highlight tracks finger live
EV_KEY BTN_TOUCH = 1          → arm finger_down
                              → reset touch_x = touch_y = -1 and
                                osk.pressed_* = -1 so we DON'T render
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
hit test in commit `a3d3b90` (the first OSK cut) was hitting
yesterday's coordinates. Commit `f958b6a` defers the press until
ABS arrives. Side effect: dragging across keys now updates the
highlight in real time, which is the right UX anyway.

## Sticky modifier behaviour

`osk_touch_release()` traps modifier-key codes and toggles the
matching `OSK` field:

```c
case KEY_LEFTSHIFT:  k->shift = !k->shift; return 0;
case KEY_LEFTCTRL:   k->ctrl  = !k->ctrl;  return 0;
case KEY_LEFTALT:    k->alt   = !k->alt;   return 0;
case KEY_PAGE_TOGGLE:k->page  = !k->page;  return 0;  /* synthetic */
default:             return code;
```

(Returning 0 means "no key dispatched.") `KEY_PAGE_TOGGLE` is
`0xFFF1` — outside the Linux `KEY_*` range so `input_ev_to_pty` won't
accidentally interpret it.

`main.c` reads the latches at dispatch time:

```c
bool eff_shift = osk.shift || osk.last_autoshift;
if (eff_shift) <send KEY_LEFTSHIFT press>
if (osk.ctrl)  <send KEY_LEFTCTRL  press>
if (osk.alt)   <send KEY_LEFTALT   press>
<send KEY_* press + release>
if (osk.alt)   <send KEY_LEFTALT   release>
if (osk.ctrl)  <send KEY_LEFTCTRL  release>
if (eff_shift) <send KEY_LEFTSHIFT release>
osk.shift = osk.ctrl = osk.alt = false;
osk.last_autoshift = false;
```

`input_ev_to_pty` keeps its own `in.shift` / `in.ctrl` / `in.alt`
state by listening for the synthetic modifier KEY events, so the
existing keyboard char-mapping logic applies unchanged.

## Visual feedback

`osk_render` fills the whole OSK region opaque dark, then draws each
cap. Three cap states:

- Default:        `0xff2c2c34`  (dark grey)
- Pressed:        `0xff5a5a64`  (lighter grey, while finger is down)
- Latched:        `0xff3a6e98`  (Termux-blue when sticky modifier
                                 is set on this cap)

Labels are drawn via `display_draw_text`, centered horizontally,
baseline approximated at `(row_h - FONT_SIZE) / 2` from the top of
the cap. A 4 px gutter (`OSK_PADDING_PX`) between caps.

## Entry points

```c
bool     osk_init        (OSK *k, int screen_w, int screen_h);
int      osk_height_for  (int screen_w, int screen_h);
uint16_t osk_hit_test    (const OSK *k, int x, int y, int *r, int *c);
uint16_t osk_touch_press (OSK *k, int x, int y);
uint16_t osk_touch_release(OSK *k, int x, int y);
void     osk_render      (DisplayDev *d, OSK *k);
```

`osk_init` writes the layout into the OSK struct and returns true.
`osk_height_for` is the size to subtract from `term_init`'s height
so cells don't overlap the keyboard region.

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
