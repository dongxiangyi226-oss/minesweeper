#ifndef RENDER_H
#define RENDER_H

#include <windows.h>
#include "game.h"

/* ---- Constants ---- */
#define CELL_SIZE       30      /* pixels per cell */
#define HEADER_HEIGHT   56      /* top status bar height */
#define TOOLBAR_HEIGHT  36      /* bottom toolbar height */
#define BORDER_SIZE     10      /* border around minefield */

/* ---- Renderer ---- */
typedef struct {
    HDC     mem_dc;             /* memory DC for double buffering */
    HBITMAP mem_bmp;            /* memory bitmap */
    HBITMAP old_bmp;
    int     buf_w, buf_h;      /* buffer dimensions */
    HFONT   font_num;          /* font for cell numbers */
    HFONT   font_header;       /* font for header (mine count, timer) */
    HFONT   font_toolbar;      /* font for toolbar buttons */
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

#endif /* RENDER_H */
