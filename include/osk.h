#ifndef OSK_H
#define OSK_H

#include "display.h"
#include "input.h"
#include <stdbool.h>
#include <stdint.h>

/* On-screen keyboard pinned to the bottom of the panel.
 *
 * Layout: extras row on top + 4 main rows. The terminal grid renders
 * above the OSK; main.c is responsible for sizing the term to
 * (screen_h - OSK_HEIGHT) so cells don't overlap. Touch hits are
 * dispatched into the existing input_ev_to_pty() path by synthesizing
 * struct input_event records with EV_KEY codes.
 *
 * Sizing model: each OskKey.span is in HALF-cells. A "1 cell" letter
 * key has span 2; SHIFT/BSP have span 3 (1.5 cells); SPACE has
 * span 10 (5 cells). Each row has its own total span (computed at
 * page-change time), so a 9-letter row naturally renders with bigger
 * keys than a 10-letter row — Gboard does the same.
 */

#define OSK_ROWS 5
#define OSK_MAX_COLS 12
#define OSK_PAGES 3

typedef struct {
  uint16_t code;      /* KEY_* from <linux/input-event-codes.h> */
  const char *label;  /* what to draw on the cap (ASCII only — display
                       * doesn't decode UTF-8 yet) */
  uint8_t span;       /* width in HALF-cells (see above) */
  bool autoshift;     /* implicit SHIFT for this dispatch (for cap labels
                       * like "{" / "|" / ":" whose key code is unshifted) */
  bool repeatable;    /* long-press auto-repeats this key (arrows, bsp) */
} OskKey;

typedef struct {
  int x, y;       /* top-left of OSK region in screen px */
  int w, h;       /* total OSK pixel size */
  int row_h;      /* per-row height */
  int rows;
  int cols_per_row[OSK_ROWS];
  int row_total_span[OSK_ROWS]; /* sum of spans for the current page;
                                 * key pixel width = span * w / total */

  bool shift;     /* sticky shift state for letter caps */
  bool ctrl;      /* sticky ctrl — clears after next keystroke */
  bool alt;       /* sticky alt  — clears after next keystroke */
  bool last_autoshift; /* true if the cap just released was tagged
                        * autoshift; main.c reads this and synthesizes
                        * a SHIFT press around the keystroke. Cleared
                        * by the next press/release pair. */
  int  page;      /* 0 = letters, 1 = numbers/symbols */
  int  pressed_row; /* -1 when no finger down */
  int  pressed_col;
  uint64_t pressed_at_ms;   /* monotonic ms when finger went down */
  uint64_t last_repeat_ms;  /* monotonic ms of last long-press fire */
} OSK;

/* Build the layout for a panel of (screen_w x screen_h). The OSK
 * occupies the bottom OSK_HEIGHT pixels. */
bool osk_init(OSK *k, int screen_w, int screen_h);

/* Pixel height the OSK reserves; main can subtract this from the
 * term grid before sizing rows. */
int  osk_height_for(int screen_w, int screen_h);

/* Hit test absolute (x,y) coords into row/col + key code.
 * Returns the KEY_* code or 0 if outside the OSK area. */
uint16_t osk_hit_test(const OSK *k, int x, int y, int *out_row, int *out_col);

/* Render OSK on top of the framebuffer. Call after term renders. */
void osk_render(DisplayDev *d, OSK *k);

/* Touch input plumbing — main.c calls these on multitouch events:
 *   touch_press: finger down at (x,y) — returns KEY_* (0 = miss)
 *   touch_release: finger up — returns KEY_* to inject (0 = miss)
 * The press/release pair lights up the cap during contact and only
 * dispatches the keystroke on lift-off, so the user can slide off a
 * key to cancel. */
uint16_t osk_touch_press(OSK *k, int x, int y, uint64_t now_ms);
uint16_t osk_touch_release(OSK *k, int x, int y);

/* Long-press repeat. main.c calls this every loop iteration with the
 * current monotonic-ms clock; if a repeatable key has been held past
 * the initial delay (~400ms) and the per-repeat interval (~60ms) has
 * elapsed since last fire, returns the KEY_* to inject and records
 * the fire time. Returns 0 otherwise. */
uint16_t osk_pump_repeat(OSK *k, uint64_t now_ms);

#endif
