#ifndef OSK_H
#define OSK_H

#include "display.h"
#include "input.h"
#include <stdbool.h>
#include <stdint.h>

/* On-screen keyboard pinned to the bottom of the panel.
 *
 * Layout: 4 rows of fixed cells. The terminal grid renders above the
 * OSK; main.c is responsible for sizing the term to (screen_h -
 * OSK_HEIGHT) so cells don't overlap. Touch hits are dispatched into
 * the existing input_ev_to_pty() path by synthesizing struct
 * input_event records with EV_KEY codes.
 */

#define OSK_ROWS 4
#define OSK_MAX_COLS 10

typedef struct {
  uint16_t code;     /* KEY_* from <linux/input-event-codes.h> */
  const char *label; /* what to draw on the cap */
  uint8_t span;      /* width in cell units (1 or more) */
} OskKey;

typedef struct {
  int x, y;       /* top-left of OSK region in screen px */
  int w, h;       /* total OSK pixel size */
  int row_h;      /* per-row height */
  int cell_w;     /* base column width (longest key spans multiple) */
  int rows;
  int cols_per_row[OSK_ROWS];

  bool shift;     /* sticky shift state for letter caps */
  int  pressed_row; /* -1 when no finger down */
  int  pressed_col;
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
uint16_t osk_touch_press(OSK *k, int x, int y);
uint16_t osk_touch_release(OSK *k, int x, int y);

#endif
