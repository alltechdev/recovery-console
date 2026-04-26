#define _GNU_SOURCE
#include "osk.h"
#include "display.h"
#include "config.h"
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

/* Layout — top row mirrors Termux's "extra keys" bar (terminal
 * controls + arrows) above 4 rows of qwerty. Keep this table the
 * single source of truth for both rendering and hit-testing. */
#define OSK_HEIGHT_PX 800
#define OSK_PADDING_PX 4

/* KEY_PAGE_TOGGLE is a synthetic code we use to flip between the
 * letters page and the numbers/symbols page. It lives outside the
 * KEY_* range used by Linux input so input_ev_to_pty's char-mapping
 * doesn't see it; main.c traps it before dispatch. */
#define KEY_PAGE_TOGGLE 0xFFF1

/* Termux-style extras bar at the top: terminal-control keys and
 * arrow keys. Same on both pages so muscle memory is preserved. */
static const OskKey row_extras[] = {
    {KEY_ESC, "ESC", 1},       {KEY_TAB, "TAB", 1},
    {KEY_LEFTCTRL, "CTRL", 1}, {KEY_LEFTALT, "ALT", 1},
    {KEY_UP, "^", 1},          {KEY_DOWN, "v", 1},
    {KEY_LEFT, "<", 1},        {KEY_RIGHT, ">", 1},
    {KEY_HOME, "Hm", 1},       {KEY_END, "End", 1},
};

/* ---------- Page 0: letters (qwerty) ---------- */
static const OskKey p0_row0[] = {
    {KEY_Q, "q", 1}, {KEY_W, "w", 1}, {KEY_E, "e", 1}, {KEY_R, "r", 1},
    {KEY_T, "t", 1}, {KEY_Y, "y", 1}, {KEY_U, "u", 1}, {KEY_I, "i", 1},
    {KEY_O, "o", 1}, {KEY_P, "p", 1},
};
static const OskKey p0_row1[] = {
    {KEY_A, "a", 1}, {KEY_S, "s", 1}, {KEY_D, "d", 1}, {KEY_F, "f", 1},
    {KEY_G, "g", 1}, {KEY_H, "h", 1}, {KEY_J, "j", 1}, {KEY_K, "k", 1},
    {KEY_L, "l", 1}, {KEY_BACKSPACE, "<x", 1},
};
static const OskKey p0_row2[] = {
    {KEY_LEFTSHIFT, "shft", 1}, {KEY_Z, "z", 1}, {KEY_X, "x", 1},
    {KEY_C, "c", 1},            {KEY_V, "v", 1}, {KEY_B, "b", 1},
    {KEY_N, "n", 1},            {KEY_M, "m", 1}, {KEY_DOT, ".", 1},
    {KEY_ENTER, "ent", 1},
};
static const OskKey p0_row3[] = {
    {KEY_PAGE_TOGGLE, "?123", 1}, {KEY_COMMA, ",", 1},
    {KEY_SPACE, "______", 6},     {KEY_MINUS, "-", 1},
    {KEY_SLASH, "/", 1},
};

/* ---------- Page 1: numbers + symbols ---------- */
static const OskKey p1_row0[] = {
    {KEY_1, "1", 1}, {KEY_2, "2", 1}, {KEY_3, "3", 1}, {KEY_4, "4", 1},
    {KEY_5, "5", 1}, {KEY_6, "6", 1}, {KEY_7, "7", 1}, {KEY_8, "8", 1},
    {KEY_9, "9", 1}, {KEY_0, "0", 1},
};
static const OskKey p1_row1[] = {
    {KEY_LEFTBRACE,  "[", 1, false}, {KEY_RIGHTBRACE,  "]", 1, false},
    {KEY_LEFTBRACE,  "{", 1, true},  {KEY_RIGHTBRACE,  "}", 1, true},
    {KEY_BACKSLASH,  "\\", 1, false},{KEY_BACKSLASH,   "|", 1, true},
    {KEY_SEMICOLON,  ";", 1, false}, {KEY_SEMICOLON,   ":", 1, true},
    {KEY_EQUAL,      "=", 1, false}, {KEY_BACKSPACE,   "<x", 1, false},
};
static const OskKey p1_row2[] = {
    {KEY_LEFTSHIFT,  "shft", 1, false}, {KEY_MINUS,    "_", 1, true},
    {KEY_4,          "$", 1, true},     {KEY_5,        "%", 1, true},
    {KEY_6,          "^", 1, true},     {KEY_7,        "&", 1, true},
    {KEY_8,          "*", 1, true},     {KEY_GRAVE,    "~", 1, true},
    {KEY_DOT,        ".", 1, false},    {KEY_ENTER,    "ent", 1, false},
};
static const OskKey p1_row3[] = {
    {KEY_PAGE_TOGGLE, "ABC", 1}, {KEY_COMMA, ",", 1},
    {KEY_SPACE, "______", 6},    {KEY_MINUS, "-", 1},
    {KEY_SLASH, "/", 1},
};

/* Page table. row_extras is shared across pages. */
static const OskKey *page0_rows[OSK_ROWS] = {row_extras, p0_row0, p0_row1, p0_row2, p0_row3};
static const int page0_count[OSK_ROWS] = {
    sizeof(row_extras) / sizeof(row_extras[0]),
    sizeof(p0_row0) / sizeof(p0_row0[0]), sizeof(p0_row1) / sizeof(p0_row1[0]),
    sizeof(p0_row2) / sizeof(p0_row2[0]), sizeof(p0_row3) / sizeof(p0_row3[0]),
};
static const OskKey *page1_rows[OSK_ROWS] = {row_extras, p1_row0, p1_row1, p1_row2, p1_row3};
static const int page1_count[OSK_ROWS] = {
    sizeof(row_extras) / sizeof(row_extras[0]),
    sizeof(p1_row0) / sizeof(p1_row0[0]), sizeof(p1_row1) / sizeof(p1_row1[0]),
    sizeof(p1_row2) / sizeof(p1_row2[0]), sizeof(p1_row3) / sizeof(p1_row3[0]),
};

static inline const OskKey **rows_for(int page) {
  return page ? page1_rows : page0_rows;
}
static inline const int *count_for(int page) {
  return page ? page1_count : page0_count;
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
  k->cell_w = screen_w / 10;
  for (int r = 0; r < OSK_ROWS; r++)
    k->cols_per_row[r] = count_for(0)[r]; /* same shape on both pages */
  k->pressed_row = -1;
  k->pressed_col = -1;
  return true;
}

/* Resolve (row, col) -> KEY_* via the layout table for the current page. */
static uint16_t key_at(const OSK *k, int row, int col) {
  if (row < 0 || row >= OSK_ROWS)
    return 0;
  const int *cnt = count_for(k->page);
  if (col < 0 || col >= cnt[row])
    return 0;
  return rows_for(k->page)[row][col].code;
}

/* Map a pixel x coordinate (already known to be inside row `row`) to
 * its column index, accounting for spans. */
static int col_at_x(const OSK *k, int row, int x) {
  int local_x = x - k->x;
  if (local_x < 0)
    return -1;
  int acc = 0;
  const int *cnt = count_for(k->page);
  const OskKey *const *rows = rows_for(k->page);
  for (int c = 0; c < cnt[row]; c++) {
    int w = rows[row][c].span * k->cell_w;
    if (local_x < acc + w)
      return c;
    acc += w;
  }
  return -1;
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

uint16_t osk_touch_press(OSK *k, int x, int y) {
  int r, c;
  uint16_t code = osk_hit_test(k, x, y, &r, &c);
  k->pressed_row = r;
  k->pressed_col = c;
  return code;
}

uint16_t osk_touch_release(OSK *k, int x, int y) {
  int r, c;
  uint16_t code = osk_hit_test(k, x, y, &r, &c);
  k->pressed_row = -1;
  k->pressed_col = -1;
  /* Stash whether the released cap is autoshift so main.c can wrap
   * the dispatch with a synthetic SHIFT. Cleared on next press. */
  k->last_autoshift = false;
  if (r >= 0 && c >= 0) {
    const OskKey *key = &rows_for(k->page)[r][c];
    if (key->autoshift)
      k->last_autoshift = true;
  }

  /* Modifier caps and the page toggle handle themselves and return 0
   * so caller doesn't try to inject a keystroke. The next regular
   * press reads the modifier state then clears it. */
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
    k->page = !k->page;
    return 0;
  default:
    return code;
  }
}

/* Pixel x for the start of column `col` in `row`. */
static int col_x(const OSK *k, int row, int col) {
  int x = k->x;
  const OskKey *const *rows = rows_for(k->page);
  for (int i = 0; i < col; i++)
    x += rows[row][i].span * k->cell_w;
  return x;
}

void osk_render(DisplayDev *d, OSK *k) {
  (void)d;
  if (!k || k->w <= 0)
    return;

  /* Background — opaque dark fill so the term grid above is visually
   * separated from the keyboard. ARGB8888, 0xff is fully opaque
   * alpha, R=0x18 G=0x18 B=0x1c (near-black grey). */
  display_draw_rect(k->x, k->y, k->w, k->h, 0xff18181c);

  /* Top border line so the OSK feels distinct from the term. */
  display_draw_rect(k->x, k->y, k->w, 2, 0xff44444c);

  const OskKey *const *rows = rows_for(k->page);
  const int *cnt = count_for(k->page);
  for (int r = 0; r < OSK_ROWS; r++) {
    int row_y = k->y + r * k->row_h;
    for (int c = 0; c < cnt[r]; c++) {
      const OskKey *key = &rows[r][c];
      int kx = col_x(k, r, c);
      int kw = key->span * k->cell_w;
      bool pressed = (r == k->pressed_row && c == k->pressed_col);
      bool latched =
          (key->code == KEY_LEFTSHIFT && k->shift) ||
          (key->code == KEY_LEFTCTRL && k->ctrl) ||
          (key->code == KEY_LEFTALT && k->alt);
      uint32_t cap_bg;
      if (latched)        cap_bg = 0xff3a6e98; /* termux-blue when sticky */
      else if (pressed)   cap_bg = 0xff5a5a64;
      else                cap_bg = 0xff2c2c34;
      uint32_t cap_fg = 0xffffffff;

      /* Cap background, leaving a 1-px gutter between caps. */
      display_draw_rect(kx + OSK_PADDING_PX, row_y + OSK_PADDING_PX,
                        kw - 2 * OSK_PADDING_PX,
                        k->row_h - 2 * OSK_PADDING_PX, cap_bg);

      /* Render label centered horizontally, baseline near vertical
       * center. We don't have font_height exposed — approximate via
       * row_h * 0.45 as the y offset. */
      int label_w = display_text_width(key->label);
      int label_x = kx + (kw - label_w) / 2;
      int label_y = row_y + (k->row_h - FONT_SIZE) / 2;
      display_draw_text(label_x, label_y, key->label, cap_fg, cap_bg);
    }
  }
}
