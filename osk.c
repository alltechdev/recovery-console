#define _GNU_SOURCE
#include "osk.h"
#include "display.h"
#include "config.h"
#include "font.h"
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

/* See osk.h for the sizing model. Spans are in HALF-cells. Most rows
 * total 20 (one full keyboard width); the home row (a-l, 9 keys × 2)
 * totals 18 and keys auto-stretch — Gboard does the same. */

#define OSK_HEIGHT_PX 800
#define OSK_GUTTER_PX 6      /* gap between cap edges (visual). */
#define OSK_RADIUS_PX 0      /* sharp caps (0 = no rounding). */

/* Long-press repeat tunables. */
#define OSK_REPEAT_DELAY_MS    400
#define OSK_REPEAT_INTERVAL_MS  60

/* Synthetic codes — outside the Linux KEY_* range so input_ev_to_pty
 * doesn't try to map them to characters. main.c traps these before
 * dispatch. */
#define KEY_PAGE_TOGGLE 0xFFF1

/* ============================================================
 * Page 0 — letters (qwerty)
 *
 * Top row is the "extras" bar: terminal-control + arrows + home/end.
 * Arrows + backspace are tagged repeatable so a long-press
 * auto-repeats. Labels are ASCII because display_draw_text doesn't
 * decode UTF-8.
 * ============================================================ */

static const OskKey p0_extras[] = {
    {KEY_ESC,      "esc",  2}, {KEY_TAB,     "tab", 2},
    {KEY_LEFTCTRL, "ctrl", 2}, {KEY_LEFTALT, "alt", 2},
    {KEY_UP,       "↑",    2, false, true}, {KEY_DOWN,  "↓", 2, false, true},
    {KEY_LEFT,     "←",    2, false, true}, {KEY_RIGHT, "→", 2, false, true},
    {KEY_HOME,     "home", 2}, {KEY_END,     "end", 2},
};
/* Row 1: q-p, 10 keys. */
static const OskKey p0_row0[] = {
    {KEY_Q, "q", 2}, {KEY_W, "w", 2}, {KEY_E, "e", 2}, {KEY_R, "r", 2},
    {KEY_T, "t", 2}, {KEY_Y, "y", 2}, {KEY_U, "u", 2}, {KEY_I, "i", 2},
    {KEY_O, "o", 2}, {KEY_P, "p", 2},
};
/* Row 2: a-l, 9 keys — naturally wider than row 1 since row total = 18. */
static const OskKey p0_row1[] = {
    {KEY_A, "a", 2}, {KEY_S, "s", 2}, {KEY_D, "d", 2}, {KEY_F, "f", 2},
    {KEY_G, "g", 2}, {KEY_H, "h", 2}, {KEY_J, "j", 2}, {KEY_K, "k", 2},
    {KEY_L, "l", 2},
};
/* Row 3: shift(1.5) + zxcvbnm + bsp(1.5) = 3 + 14 + 3 = 20. */
static const OskKey p0_row2[] = {
    {KEY_LEFTSHIFT, "⇧", 3},
    {KEY_Z, "z", 2}, {KEY_X, "x", 2}, {KEY_C, "c", 2}, {KEY_V, "v", 2},
    {KEY_B, "b", 2}, {KEY_N, "n", 2}, {KEY_M, "m", 2},
    {KEY_BACKSPACE, "⌫", 3, false, true},
};
/* Row 4: page(1.5) + ',' + space(5) + '.' + enter(1.5). */
static const OskKey p0_row3[] = {
    {KEY_PAGE_TOGGLE, "?123", 3}, {KEY_COMMA, ",", 2},
    {KEY_SPACE,       " ",   10}, {KEY_DOT,   ".", 2},
    {KEY_ENTER,       "⏎",   3},
};

/* ============================================================
 * Page 1 — numbers + ALL printable symbols (except ^)
 *
 * No extras row on this page — those 10 slots become more symbols.
 * Returning to page 0 (ABC) brings them back.
 * ============================================================ */

/* Row 0: 1234567890. */
static const OskKey p1_row0[] = {
    {KEY_1, "1", 2}, {KEY_2, "2", 2}, {KEY_3, "3", 2}, {KEY_4, "4", 2},
    {KEY_5, "5", 2}, {KEY_6, "6", 2}, {KEY_7, "7", 2}, {KEY_8, "8", 2},
    {KEY_9, "9", 2}, {KEY_0, "0", 2},
};
/* Row 1: @ # $ % & * - + ( )  — most-used punctuation. */
static const OskKey p1_row1[] = {
    {KEY_2,     "@", 2, true},  {KEY_3,     "#", 2, true},
    {KEY_4,     "$", 2, true},  {KEY_5,     "%", 2, true},
    {KEY_7,     "&", 2, true},  {KEY_8,     "*", 2, true},
    {KEY_MINUS, "-", 2, false}, {KEY_EQUAL, "+", 2, true},
    {KEY_9,     "(", 2, true},  {KEY_0,     ")", 2, true},
};
/* Row 2: " ' : ; ! ? = / [ ]  — quotes, terminators, brackets. */
static const OskKey p1_row2[] = {
    {KEY_APOSTROPHE, "\"", 2, true},  {KEY_APOSTROPHE, "'",  2, false},
    {KEY_SEMICOLON,  ":",  2, true},  {KEY_SEMICOLON,  ";",  2, false},
    {KEY_1,          "!",  2, true},  {KEY_SLASH,      "?",  2, true},
    {KEY_EQUAL,      "=",  2, false}, {KEY_SLASH,      "/",  2, false},
    {KEY_LEFTBRACE,  "[",  2, false}, {KEY_RIGHTBRACE, "]",  2, false},
};
/* Row 3: ` ~ ^ < > _ | \ { } <bsp> — 11 cells. Row total span is 22
 * which the per-row stretch math accepts: this row's keys are
 * slightly thinner than the 10-cell rows above. All printable ASCII
 * is now reachable. Long-press repeat on bsp. */
static const OskKey p1_row3[] = {
    {KEY_GRAVE,      "`",  2, false}, {KEY_GRAVE,      "~",  2, true},
    {KEY_6,          "^",  2, true},
    {KEY_COMMA,      "<",  2, true},  {KEY_DOT,        ">",  2, true},
    {KEY_MINUS,      "_",  2, true},  {KEY_BACKSLASH,  "|",  2, true},
    {KEY_BACKSLASH,  "\\", 2, false}, {KEY_LEFTBRACE,  "{",  2, true},
    {KEY_RIGHTBRACE, "}",  2, true},
    {KEY_BACKSPACE,  "⌫", 2, false, true},
};
/* Row 4: same shape as page 0's bottom — toggle/comma/space/period/enter. */
static const OskKey p1_row3_bottom[] = {
    {KEY_PAGE_TOGGLE, "ABC", 3}, {KEY_COMMA, ",", 2},
    {KEY_SPACE,       " ",  10}, {KEY_DOT,   ".", 2},
    {KEY_ENTER,       "ent", 3},
};

/* ============================================================
 * Page tables
 * ============================================================ */

static const OskKey *page0_rows[OSK_ROWS] = {p0_extras, p0_row0, p0_row1, p0_row2, p0_row3};
static const int     page0_count[OSK_ROWS] = {
    sizeof(p0_extras)/sizeof(p0_extras[0]),
    sizeof(p0_row0)/sizeof(p0_row0[0]),  sizeof(p0_row1)/sizeof(p0_row1[0]),
    sizeof(p0_row2)/sizeof(p0_row2[0]),  sizeof(p0_row3)/sizeof(p0_row3[0]),
};
static const OskKey *page1_rows[OSK_ROWS] = {p1_row0, p1_row1, p1_row2, p1_row3, p1_row3_bottom};
static const int     page1_count[OSK_ROWS] = {
    sizeof(p1_row0)/sizeof(p1_row0[0]),  sizeof(p1_row1)/sizeof(p1_row1[0]),
    sizeof(p1_row2)/sizeof(p1_row2[0]),  sizeof(p1_row3)/sizeof(p1_row3[0]),
    sizeof(p1_row3_bottom)/sizeof(p1_row3_bottom[0]),
};

static inline const OskKey **rows_for(int page) {
  return (page == 1) ? page1_rows : page0_rows;
}
static inline const int *count_for(int page) {
  return (page == 1) ? page1_count : page0_count;
}

/* Recompute per-row total span for the current page. */
static void recompute_spans(OSK *k) {
  const OskKey *const *rows = rows_for(k->page);
  const int *cnt = count_for(k->page);
  for (int r = 0; r < OSK_ROWS; r++) {
    int total = 0;
    for (int c = 0; c < cnt[r]; c++)
      total += rows[r][c].span;
    k->row_total_span[r] = total > 0 ? total : 1;
    k->cols_per_row[r] = cnt[r];
  }
}

int osk_height_for(int screen_w, int screen_h) {
  (void)screen_w;
  (void)screen_h;
  return OSK_HEIGHT_PX;
}

bool osk_init(OSK *k, int screen_w, int screen_h) {
  if (!k)
    return false;
  memset(k, 0, sizeof(*k));
  k->h = OSK_HEIGHT_PX;
  k->w = screen_w;
  k->x = 0;
  k->y = screen_h - OSK_HEIGHT_PX;
  k->rows = OSK_ROWS;
  k->row_h = OSK_HEIGHT_PX / OSK_ROWS;
  k->page = 0;
  recompute_spans(k);
  k->pressed_row = -1;
  k->pressed_col = -1;
  return true;
}

/* Pixel width of cell `col` in `row`, given the current page. */
static int cell_w_at(const OSK *k, int row, int col) {
  const OskKey *const *rows = rows_for(k->page);
  return rows[row][col].span * k->w / k->row_total_span[row];
}

/* Pixel x of the start of column `col` in `row`. */
static int col_x(const OSK *k, int row, int col) {
  int x = k->x;
  for (int i = 0; i < col; i++)
    x += cell_w_at(k, row, i);
  return x;
}

/* Map a pixel x (already known to be inside row `row`) to its column. */
static int col_at_x(const OSK *k, int row, int x) {
  int local = x - k->x;
  if (local < 0)
    return -1;
  int acc = 0;
  const int *cnt = count_for(k->page);
  for (int c = 0; c < cnt[row]; c++) {
    int w = cell_w_at(k, row, c);
    if (local < acc + w)
      return c;
    acc += w;
  }
  return -1;
}

static uint16_t key_at(const OSK *k, int row, int col) {
  if (row < 0 || row >= OSK_ROWS)
    return 0;
  const int *cnt = count_for(k->page);
  if (col < 0 || col >= cnt[row])
    return 0;
  return rows_for(k->page)[row][col].code;
}

uint16_t osk_hit_test(const OSK *k, int x, int y, int *out_row, int *out_col) {
  if (out_row)
    *out_row = -1;
  if (out_col)
    *out_col = -1;
  if (!k || y < k->y || y >= k->y + k->h)
    return 0;
  if (x < k->x || x >= k->x + k->w)
    return 0;
  int row = (y - k->y) / k->row_h;
  if (row >= k->rows)
    return 0;
  int col = col_at_x(k, row, x);
  if (col < 0)
    return 0;
  if (out_row)
    *out_row = row;
  if (out_col)
    *out_col = col;
  return key_at(k, row, col);
}

uint16_t osk_touch_press(OSK *k, int x, int y, uint64_t now_ms) {
  int r, c;
  uint16_t code = osk_hit_test(k, x, y, &r, &c);
  k->pressed_row = r;
  k->pressed_col = c;
  k->pressed_at_ms = now_ms;
  k->last_repeat_ms = 0;
  return code;
}

uint16_t osk_touch_release(OSK *k, int x, int y) {
  int r, c;
  uint16_t code = osk_hit_test(k, x, y, &r, &c);
  k->pressed_row = -1;
  k->pressed_col = -1;
  k->pressed_at_ms = 0;
  k->last_repeat_ms = 0;
  k->last_autoshift = false;
  if (r >= 0 && c >= 0) {
    const OskKey *key = &rows_for(k->page)[r][c];
    if (key->autoshift)
      k->last_autoshift = true;
  }

  switch (code) {
  case KEY_LEFTSHIFT:
    k->shift = !k->shift;
    return 0;
  case KEY_LEFTCTRL:
    k->ctrl = !k->ctrl;
    return 0;
  case KEY_LEFTALT:
    k->alt = !k->alt;
    return 0;
  case KEY_PAGE_TOGGLE:
    k->page = (k->page + 1) % 2;
    recompute_spans(k);
    return 0;
  default:
    return code;
  }
}

uint16_t osk_pump_repeat(OSK *k, uint64_t now_ms) {
  if (!k || k->pressed_row < 0 || k->pressed_col < 0)
    return 0;
  /* Only the current page's table — pressed_row/col index into it. */
  const OskKey *key =
      &rows_for(k->page)[k->pressed_row][k->pressed_col];
  if (!key->repeatable)
    return 0;
  if (now_ms - k->pressed_at_ms < OSK_REPEAT_DELAY_MS)
    return 0;
  if (k->last_repeat_ms == 0) {
    /* First repeat fires exactly at REPEAT_DELAY_MS after press. */
    k->last_repeat_ms = now_ms;
    return key->code;
  }
  if (now_ms - k->last_repeat_ms < OSK_REPEAT_INTERVAL_MS)
    return 0;
  k->last_repeat_ms = now_ms;
  return key->code;
}

/* ============================================================
 * Rendering — Gboard-ish dark theme with rounded caps.
 * ============================================================ */
void osk_render(DisplayDev *d, OSK *k) {
  (void)d;
  if (!k || k->w <= 0)
    return;

  const uint32_t COL_BG       = 0xff1f1f23;
  const uint32_t COL_BORDER   = 0xff44444c;
  const uint32_t COL_CAP      = 0xff3a3a42;
  const uint32_t COL_CAP_MOD  = 0xff2c2c34;
  const uint32_t COL_PRESSED  = 0xff56565e;
  const uint32_t COL_LATCHED  = 0xff3a6e98;
  const uint32_t COL_HILITE   = 0xff48484e;
  const uint32_t COL_LABEL    = 0xffe8e8ee;

  display_draw_rect(k->x, k->y, k->w, k->h, COL_BG);
  display_draw_rect(k->x, k->y, k->w, 2, COL_BORDER);

  const OskKey *const *rows = rows_for(k->page);
  const int *cnt = count_for(k->page);
  for (int r = 0; r < OSK_ROWS; r++) {
    int row_y = k->y + r * k->row_h;
    for (int c = 0; c < cnt[r]; c++) {
      const OskKey *key = &rows[r][c];
      int kx = col_x(k, r, c);
      int kw = cell_w_at(k, r, c);
      bool pressed = (r == k->pressed_row && c == k->pressed_col);
      bool latched =
          (key->code == KEY_LEFTSHIFT && k->shift) ||
          (key->code == KEY_LEFTCTRL && k->ctrl) ||
          (key->code == KEY_LEFTALT  && k->alt);

      bool is_modifier_cap =
          (key->code == KEY_LEFTSHIFT || key->code == KEY_LEFTCTRL ||
           key->code == KEY_LEFTALT   || key->code == KEY_BACKSPACE ||
           key->code == KEY_ENTER     || key->code == KEY_TAB ||
           key->code == KEY_ESC       || key->code == KEY_PAGE_TOGGLE ||
           key->code == KEY_SPACE     ||
           /* digits — match the spacebar's darker shade so the
            * number row reads as a separate band from the symbols. */
           (key->code >= KEY_1 && key->code <= KEY_0 && !key->autoshift));

      uint32_t cap_bg;
      if (latched)        cap_bg = COL_LATCHED;
      else if (pressed)   cap_bg = COL_PRESSED;
      else if (is_modifier_cap) cap_bg = COL_CAP_MOD;
      else                cap_bg = COL_CAP;

      int rx = kx + OSK_GUTTER_PX / 2;
      int ry = row_y + OSK_GUTTER_PX / 2;
      int rw = kw - OSK_GUTTER_PX;
      int rh = k->row_h - OSK_GUTTER_PX;
      display_draw_rounded_rect(rx, ry, rw, rh, OSK_RADIUS_PX, cap_bg);

      if (!pressed && !latched)
        display_draw_rounded_rect(rx, ry, rw, 1, OSK_RADIUS_PX, COL_HILITE);

      /* If shift is latched and this is a single-char ASCII letter,
       * draw the uppercase variant so the cap reflects state. */
      const char *label = key->label;
      char up_buf[8];
      if (k->shift && key->code >= KEY_A && key->code <= KEY_Z &&
          label[0] >= 'a' && label[0] <= 'z' && label[1] == 0) {
        up_buf[0] = (char)(label[0] - 'a' + 'A');
        up_buf[1] = '\0';
        label = up_buf;
      }

      int label_w = display_text_width_osk(label);
      int label_x = kx + (kw - label_w) / 2;
      int baseline_y = row_y + (k->row_h - font_baseline_osk()) / 2;
      display_draw_text_osk(label_x, baseline_y, label, COL_LABEL, cap_bg);
    }
  }
}
