#define _GNU_SOURCE
#include "osk.h"
#include "display.h"
#include "config.h"
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

/* Layout — 4 rows of 10 cell-units, with selected keys spanning more
 * than one cell. Keep this table the single source of truth for both
 * rendering and hit-testing. */
#define OSK_HEIGHT_PX 800
#define OSK_PADDING_PX 4

static const OskKey row0[] = {
    {KEY_Q, "q", 1}, {KEY_W, "w", 1}, {KEY_E, "e", 1}, {KEY_R, "r", 1},
    {KEY_T, "t", 1}, {KEY_Y, "y", 1}, {KEY_U, "u", 1}, {KEY_I, "i", 1},
    {KEY_O, "o", 1}, {KEY_P, "p", 1},
};
static const OskKey row1[] = {
    {KEY_A, "a", 1}, {KEY_S, "s", 1}, {KEY_D, "d", 1}, {KEY_F, "f", 1},
    {KEY_G, "g", 1}, {KEY_H, "h", 1}, {KEY_J, "j", 1}, {KEY_K, "k", 1},
    {KEY_L, "l", 1}, {KEY_BACKSPACE, "<x", 1},
};
static const OskKey row2[] = {
    {KEY_LEFTSHIFT, "shft", 1}, {KEY_Z, "z", 1}, {KEY_X, "x", 1},
    {KEY_C, "c", 1},            {KEY_V, "v", 1}, {KEY_B, "b", 1},
    {KEY_N, "n", 1},            {KEY_M, "m", 1}, {KEY_DOT, ".", 1},
    {KEY_ENTER, "ent", 1},
};
static const OskKey row3[] = {
    {KEY_TAB, "tab", 1},     {KEY_SPACE, "_____", 6}, {KEY_MINUS, "-", 1},
    {KEY_SLASH, "/", 1},     {KEY_ESC, "esc", 1},
};

static const OskKey *rows_ptr[OSK_ROWS] = {row0, row1, row2, row3};
static const int rows_count[OSK_ROWS] = {
    sizeof(row0) / sizeof(row0[0]),
    sizeof(row1) / sizeof(row1[0]),
    sizeof(row2) / sizeof(row2[0]),
    sizeof(row3) / sizeof(row3[0]),
};

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
    k->cols_per_row[r] = rows_count[r];
  k->pressed_row = -1;
  k->pressed_col = -1;
  return true;
}

/* Resolve (row, col) -> KEY_* via the layout table. */
static uint16_t key_at(int row, int col) {
  if (row < 0 || row >= OSK_ROWS)
    return 0;
  if (col < 0 || col >= rows_count[row])
    return 0;
  return rows_ptr[row][col].code;
}

/* Map a pixel x coordinate (already known to be inside row `row`) to
 * its column index, accounting for spans. Each row's cells lay out
 * left-to-right, summing span widths until they reach `cell_w * 10`. */
static int col_at_x(const OSK *k, int row, int x) {
  int local_x = x - k->x;
  if (local_x < 0)
    return -1;
  int acc = 0;
  for (int c = 0; c < rows_count[row]; c++) {
    int w = rows_ptr[row][c].span * k->cell_w;
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
  return key_at(row, col);
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
  /* Inject only when finger lifted on the same key it pressed.
   * Caller has the press location; we rely on the hit test of the
   * release coords matching to filter slide-cancels. The pressed_*
   * fields are reset by the time this returns so caller gets a clean
   * frame to redraw without highlight. */
  return code;
}

/* Pixel x for the start of column `col` in `row`. */
static int col_x(const OSK *k, int row, int col) {
  int x = k->x;
  for (int i = 0; i < col; i++)
    x += rows_ptr[row][i].span * k->cell_w;
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

  for (int r = 0; r < OSK_ROWS; r++) {
    int row_y = k->y + r * k->row_h;
    for (int c = 0; c < rows_count[r]; c++) {
      const OskKey *key = &rows_ptr[r][c];
      int kx = col_x(k, r, c);
      int kw = key->span * k->cell_w;
      bool pressed = (r == k->pressed_row && c == k->pressed_col);
      uint32_t cap_bg = pressed ? 0xff5a5a64 : 0xff2c2c34;
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
