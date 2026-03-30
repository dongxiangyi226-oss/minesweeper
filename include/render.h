#ifndef RENDER_H
#define RENDER_H

#include <windows.h>
#include "game.h"

/* ---- Color Theme ---- */
typedef struct {
    unsigned long bg, cell_raised, cell_revealed, cell_pressed;
    unsigned long border_white, border_ltgray, border_dkgray, border_black;
    unsigned long led_bg, led_fg;
    unsigned long toolbar_bg, toolbar_active;
    unsigned long fog_color, explode_bg, mine_color, flag_color, cursor_color;
    unsigned long num_colors[9]; /* [0] unused, [1]-[8] */
} ColorTheme;

/* ---- Constants ---- */
#define CELL_SIZE       30
#define HEADER_HEIGHT   56
#define TOOLBAR_HEIGHT  36
#define BORDER_SIZE     10
#define THEME_COUNT     4

/* ---- Renderer ---- */
typedef struct {
    HDC     mem_dc;
    HBITMAP mem_bmp;
    HBITMAP old_bmp;
    int     buf_w, buf_h;
    HFONT   font_num;
    HFONT   font_header;
    HFONT   font_toolbar;
    int     theme_index;        /* current theme 0-3 */
} Renderer;

/* Create / destroy renderer */
Renderer *render_create(HWND hwnd, int w, int h);
void      render_destroy(Renderer *r);
void      render_resize(Renderer *r, HWND hwnd, int w, int h);

/* Main paint function - draws everything to mem_dc then blits */
void render_paint(Renderer *r, HWND hwnd, Game *g, double *prob_map);

/* Calculate window client size needed for given board */
void render_calc_window_size(int board_w, int board_h, int *client_w, int *client_h);

/* Convert pixel (px, py) to cell (cx, cy). Returns 1 if on grid. */
int  render_pixel_to_cell(int board_w, int board_h, int px, int py, int *cx, int *cy);

/* Check if pixel is on toolbar button. Returns button index or -1. */
int  render_toolbar_hit(int board_w, int board_h, int client_w, int px, int py);

/* Check if pixel is on the face button in header. */
int  render_face_hit(int board_w, int client_w, int px, int py);

/* Theme management */
void render_set_theme(Renderer *r, int index);
const ColorTheme *render_get_theme(Renderer *r);

#endif /* RENDER_H */
