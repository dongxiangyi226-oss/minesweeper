/*
 * render.c  --  Win32 GDI rendering for Minesweeper
 *
 * Windows 7 Minesweeper style: 3D sunken frames, proper cell shading,
 * chord press visual feedback, exploded mine, LED counters, face button.
 *
 * All text is wide-character (wchar_t / L"...") for Unicode/Chinese support.
 * Uses W-suffix Win32 APIs throughout.
 */

#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Colour constants                                                   */
/* ------------------------------------------------------------------ */
#define CLR_FACE_BG     RGB(192, 192, 192)   /* classic Win gray      */
#define CLR_CELL_BG     RGB(192, 192, 192)   /* unrevealed fill       */
#define CLR_CELL_REV    RGB(200, 200, 200)   /* revealed fill         */
#define CLR_WHITE       RGB(255, 255, 255)
#define CLR_LTGRAY      RGB(223, 223, 223)
#define CLR_DKGRAY      RGB(128, 128, 128)
#define CLR_BLACK       RGB(0,   0,   0)
#define CLR_RED         RGB(255, 0,   0)
#define CLR_LED_BG      RGB(16,  16,  16)
#define CLR_LED_FG      RGB(255, 0,   0)
#define CLR_TOOLBAR_BG  RGB(212, 208, 200)   /* XP-ish toolbar gray   */
#define CLR_FOG         RGB(32,  32,  32)
#define CLR_EXPLODE_BG  RGB(255, 0,   0)
#define CLR_BORDER_LT   RGB(255, 255, 255)   /* sunken frame light    */
#define CLR_BORDER_DK   RGB(128, 128, 128)   /* sunken frame dark     */
#define CLR_BORDER_BLK  RGB(64,  64,  64)    /* outer shadow          */

/* ------------------------------------------------------------------ */
/*  Number colours  (index 1..8)                                       */
/* ------------------------------------------------------------------ */
static const COLORREF NUM_COLORS[9] = {
    RGB(0,   0,   0),      /* 0 - unused */
    RGB(0,   0,   255),    /* 1 - blue */
    RGB(0,   128, 0),      /* 2 - green */
    RGB(255, 0,   0),      /* 3 - red */
    RGB(0,   0,   128),    /* 4 - dark blue */
    RGB(128, 0,   0),      /* 5 - dark red */
    RGB(0,   128, 128),    /* 6 - teal */
    RGB(0,   0,   0),      /* 7 - black */
    RGB(128, 128, 128)     /* 8 - gray */
};

/* ================================================================== */
/*  Helper: draw a sunken frame (Win classic inset border)             */
/*  Draws a 2px border: dark-gray/black on top-left, white on         */
/*  bottom-right. The interior is NOT filled.                          */
/* ================================================================== */
static void draw_sunken_frame(HDC hdc, int x, int y, int w, int h)
{
    HPEN penDk   = CreatePen(PS_SOLID, 1, CLR_DKGRAY);
    HPEN penBlk  = CreatePen(PS_SOLID, 1, CLR_BORDER_BLK);
    HPEN penWh   = CreatePen(PS_SOLID, 1, CLR_WHITE);
    HPEN penLt   = CreatePen(PS_SOLID, 1, CLR_LTGRAY);
    HPEN oldPen;

    /* Outer top-left: dark gray */
    oldPen = (HPEN)SelectObject(hdc, penDk);
    MoveToEx(hdc, x, y + h - 2, NULL);
    LineTo(hdc, x, y);
    LineTo(hdc, x + w - 1, y);
    /* Inner top-left: near-black */
    SelectObject(hdc, penBlk);
    MoveToEx(hdc, x + 1, y + h - 3, NULL);
    LineTo(hdc, x + 1, y + 1);
    LineTo(hdc, x + w - 2, y + 1);

    /* Outer bottom-right: white */
    SelectObject(hdc, penWh);
    MoveToEx(hdc, x + w - 1, y, NULL);
    LineTo(hdc, x + w - 1, y + h - 1);
    LineTo(hdc, x - 1, y + h - 1);
    /* Inner bottom-right: light gray */
    SelectObject(hdc, penLt);
    MoveToEx(hdc, x + w - 2, y + 1, NULL);
    LineTo(hdc, x + w - 2, y + h - 2);
    LineTo(hdc, x, y + h - 2);

    SelectObject(hdc, oldPen);
    DeleteObject(penDk);
    DeleteObject(penBlk);
    DeleteObject(penWh);
    DeleteObject(penLt);
}

/* ================================================================== */
/*  Helper: draw a raised frame (button look, 2px highlight/shadow)    */
/*  This is the classic Win32 raised button border.                    */
/* ================================================================== */
static void draw_raised_frame(HDC hdc, int x, int y, int w, int h)
{
    HPEN penWh  = CreatePen(PS_SOLID, 1, CLR_WHITE);
    HPEN penLt  = CreatePen(PS_SOLID, 1, CLR_LTGRAY);
    HPEN penDk  = CreatePen(PS_SOLID, 1, CLR_DKGRAY);
    HPEN penBlk = CreatePen(PS_SOLID, 1, CLR_BLACK);
    HPEN oldPen;

    /* Outer white highlight (top-left) */
    oldPen = (HPEN)SelectObject(hdc, penWh);
    MoveToEx(hdc, x, y + h - 2, NULL);
    LineTo(hdc, x, y);
    LineTo(hdc, x + w - 1, y);
    /* Inner light highlight */
    SelectObject(hdc, penLt);
    MoveToEx(hdc, x + 1, y + h - 3, NULL);
    LineTo(hdc, x + 1, y + 1);
    LineTo(hdc, x + w - 2, y + 1);

    /* Outer black shadow (bottom-right) */
    SelectObject(hdc, penBlk);
    MoveToEx(hdc, x + w - 1, y, NULL);
    LineTo(hdc, x + w - 1, y + h - 1);
    LineTo(hdc, x - 1, y + h - 1);
    /* Inner dark shadow */
    SelectObject(hdc, penDk);
    MoveToEx(hdc, x + w - 2, y + 1, NULL);
    LineTo(hdc, x + w - 2, y + h - 2);
    LineTo(hdc, x, y + h - 2);

    SelectObject(hdc, oldPen);
    DeleteObject(penWh);
    DeleteObject(penLt);
    DeleteObject(penDk);
    DeleteObject(penBlk);
}

/* ================================================================== */
/*  Helper: fill a rect with solid colour                              */
/* ================================================================== */
static void fill_rect_color(HDC hdc, int x, int y, int w, int h, COLORREF clr)
{
    RECT rc = { x, y, x + w, y + h };
    HBRUSH br = CreateSolidBrush(clr);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

/* ================================================================== */
/*  Helper: 3D raised cell (unrevealed, classic minesweeper look)      */
/*  2px white top-left, 1px dark shadow bottom-right, gray fill        */
/* ================================================================== */
static void draw_cell_raised(HDC hdc, int x, int y, int sz)
{
    HPEN penWh  = CreatePen(PS_SOLID, 1, CLR_WHITE);
    HPEN penDk  = CreatePen(PS_SOLID, 1, CLR_DKGRAY);
    HPEN oldPen;

    /* Fill with standard gray */
    fill_rect_color(hdc, x, y, sz, sz, CLR_CELL_BG);

    /* 2px white highlight on top and left edges */
    oldPen = (HPEN)SelectObject(hdc, penWh);
    /* Outer row */
    MoveToEx(hdc, x, y + sz - 1, NULL);
    LineTo(hdc, x, y);
    LineTo(hdc, x + sz - 1, y);
    /* Inner row */
    MoveToEx(hdc, x + 1, y + sz - 2, NULL);
    LineTo(hdc, x + 1, y + 1);
    LineTo(hdc, x + sz - 2, y + 1);

    /* 1px dark shadow on bottom and right edges */
    SelectObject(hdc, penDk);
    MoveToEx(hdc, x + sz - 1, y, NULL);
    LineTo(hdc, x + sz - 1, y + sz - 1);
    LineTo(hdc, x - 1, y + sz - 1);

    SelectObject(hdc, oldPen);
    DeleteObject(penWh);
    DeleteObject(penDk);
}

/* ================================================================== */
/*  Helper: pressed cell (chord/click feedback -- flat sunken look)    */
/* ================================================================== */
static void draw_cell_pressed(HDC hdc, int x, int y, int sz)
{
    /* Fill slightly darker to indicate press */
    fill_rect_color(hdc, x, y, sz, sz, CLR_CELL_REV);

    /* 1px dark border on top-left, giving a sunken look */
    HPEN penDk = CreatePen(PS_SOLID, 1, CLR_DKGRAY);
    HPEN oldPen = (HPEN)SelectObject(hdc, penDk);
    MoveToEx(hdc, x, y + sz - 1, NULL);
    LineTo(hdc, x, y);
    LineTo(hdc, x + sz, y);
    MoveToEx(hdc, x, y, NULL);
    LineTo(hdc, x, y + sz);
    SelectObject(hdc, oldPen);
    DeleteObject(penDk);
}

/* ================================================================== */
/*  Helper: revealed cell (very subtle border, lighter gray fill)      */
/* ================================================================== */
static void draw_cell_revealed(HDC hdc, int x, int y, int sz, COLORREF bg)
{
    fill_rect_color(hdc, x, y, sz, sz, bg);

    /* Very subtle 1px border (slightly darker than fill) */
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(175, 175, 175));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, x, y, x + sz, y + sz);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
}

/* ================================================================== */
/*  Helper: draw flag (red triangle on pole with base)                 */
/* ================================================================== */
static void draw_flag(HDC hdc, int cx, int cy, int sz)
{
    int mx  = cx + sz / 2;
    int top = cy + 4;
    int bot = cy + sz - 5;

    /* Flag pole */
    HPEN polePen = CreatePen(PS_SOLID, 2, CLR_BLACK);
    HPEN oldPen = (HPEN)SelectObject(hdc, polePen);
    MoveToEx(hdc, mx, top, NULL);
    LineTo(hdc, mx, bot);
    SelectObject(hdc, oldPen);
    DeleteObject(polePen);

    /* Base line */
    HPEN basePen = CreatePen(PS_SOLID, 2, CLR_BLACK);
    oldPen = (HPEN)SelectObject(hdc, basePen);
    MoveToEx(hdc, mx - 5, bot, NULL);
    LineTo(hdc, mx + 6, bot);
    /* Small foot */
    MoveToEx(hdc, mx - 3, bot + 2, NULL);
    LineTo(hdc, mx + 4, bot + 2);
    SelectObject(hdc, oldPen);
    DeleteObject(basePen);

    /* Red triangle flag */
    HBRUSH redBr = CreateSolidBrush(CLR_RED);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, redBr);
    HPEN flagPen = CreatePen(PS_SOLID, 1, RGB(200, 0, 0));
    oldPen = (HPEN)SelectObject(hdc, flagPen);
    POINT pts[3];
    pts[0].x = mx;       pts[0].y = top;
    pts[1].x = mx;       pts[1].y = top + 9;
    pts[2].x = mx - 8;   pts[2].y = top + 4;
    Polygon(hdc, pts, 3);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(redBr);
    DeleteObject(flagPen);
}

/* ================================================================== */
/*  Helper: draw mine (black circle with spikes)                       */
/* ================================================================== */
static void draw_mine(HDC hdc, int cx, int cy, int sz)
{
    int midx = cx + sz / 2;
    int midy = cy + sz / 2;
    int rad  = sz / 2 - 5;

    /* Spikes (cross lines) */
    HPEN spkPen = CreatePen(PS_SOLID, 2, CLR_BLACK);
    HPEN oldPen = (HPEN)SelectObject(hdc, spkPen);
    /* Vertical */
    MoveToEx(hdc, midx, midy - rad - 2, NULL);
    LineTo(hdc, midx, midy + rad + 2);
    /* Horizontal */
    MoveToEx(hdc, midx - rad - 2, midy, NULL);
    LineTo(hdc, midx + rad + 2, midy);
    /* Diagonal */
    int doff = (int)(rad * 0.7) + 1;
    MoveToEx(hdc, midx - doff, midy - doff, NULL);
    LineTo(hdc, midx + doff, midy + doff);
    MoveToEx(hdc, midx + doff, midy - doff, NULL);
    LineTo(hdc, midx - doff, midy + doff);
    SelectObject(hdc, oldPen);
    DeleteObject(spkPen);

    /* Main body (filled circle) */
    HBRUSH br = CreateSolidBrush(CLR_BLACK);
    HPEN   pen = CreatePen(PS_SOLID, 1, CLR_BLACK);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    oldPen = (HPEN)SelectObject(hdc, pen);
    Ellipse(hdc, midx - rad, midy - rad, midx + rad, midy + rad);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);

    /* Specular highlight (small white dot in upper-left) */
    HBRUSH wh = CreateSolidBrush(CLR_WHITE);
    HPEN wpen = CreatePen(PS_SOLID, 1, CLR_WHITE);
    oldBr = (HBRUSH)SelectObject(hdc, wh);
    oldPen = (HPEN)SelectObject(hdc, wpen);
    int hlr = 2;
    Ellipse(hdc, midx - rad / 2 - hlr, midy - rad / 2 - hlr,
                 midx - rad / 2 + hlr, midy - rad / 2 + hlr);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(wh);
    DeleteObject(wpen);
}

/* ================================================================== */
/*  Helper: draw red X over a cell (for wrong flags on loss)           */
/* ================================================================== */
static void draw_x(HDC hdc, int cx, int cy, int sz)
{
    HPEN pen = CreatePen(PS_SOLID, 2, CLR_RED);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    int pad = 3;
    MoveToEx(hdc, cx + pad, cy + pad, NULL);
    LineTo(hdc, cx + sz - pad, cy + sz - pad);
    MoveToEx(hdc, cx + sz - pad, cy + pad, NULL);
    LineTo(hdc, cx + pad, cy + sz - pad);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

/* ================================================================== */
/*  Helper: LED-style digit display with sunken 2px border             */
/* ================================================================== */
static void draw_led_display(HDC hdc, HFONT font, int x, int y, int w, int h,
                             int value)
{
    /* Draw 2px sunken border around the LED panel */
    draw_sunken_frame(hdc, x, y, w, h);

    /* Fill interior with near-black */
    RECT inner = { x + 2, y + 2, x + w - 2, y + h - 2 };
    HBRUSH brBlack = CreateSolidBrush(CLR_LED_BG);
    FillRect(hdc, &inner, brBlack);
    DeleteObject(brBlack);

    /* Clamp display value */
    if (value > 999) value = 999;
    if (value < -99) value = -99;

    wchar_t buf[8];
    _snwprintf(buf, 8, L"%03d", value);

    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_LED_FG);
    DrawTextW(hdc, buf, -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

/* ================================================================== */
/*  Helper: check if cell is visible in fog mode                       */
/* ================================================================== */
static int is_visible_in_fog(Board *b, int cx, int cy, int fog_radius)
{
    int x0 = cx - fog_radius; if (x0 < 0) x0 = 0;
    int y0 = cy - fog_radius; if (y0 < 0) y0 = 0;
    int x1 = cx + fog_radius; if (x1 >= b->width)  x1 = b->width - 1;
    int y1 = cy + fog_radius; if (y1 >= b->height) y1 = b->height - 1;

    for (int ry = y0; ry <= y1; ry++) {
        for (int rx = x0; rx <= x1; rx++) {
            Cell *c = board_cell(b, rx, ry);
            if (c->flags & CELL_REVEALED) {
                int dx = abs(cx - rx);
                int dy = abs(cy - ry);
                int dist = dx > dy ? dx : dy;  /* Chebyshev */
                if (dist <= fog_radius)
                    return 1;
            }
        }
    }
    return 0;
}

/* ================================================================== */
/*  Helper: heatmap colour interpolation (0=green, 0.5=yellow, 1=red) */
/* ================================================================== */
static COLORREF heatmap_color(double prob)
{
    if (prob < 0.0) prob = 0.0;
    if (prob > 1.0) prob = 1.0;

    int r, g, bv;
    if (prob < 0.5) {
        double t = prob / 0.5;
        r  = (int)(t * 255);
        g  = 255;
        bv = 0;
    } else {
        double t = (prob - 0.5) / 0.5;
        r  = 255;
        g  = (int)((1.0 - t) * 255);
        bv = 0;
    }
    return RGB(r, g, bv);
}

/* ================================================================== */
/*  Helper: check if (cx,cy) is within chord press 3x3 area           */
/* ================================================================== */
static int is_in_chord_press(Game *g, int cx, int cy)
{
    if (!g->press_active) return 0;
    int dx = cx - g->press_cx;
    int dy = cy - g->press_cy;
    return (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
}

/* ================================================================== */
/*  Helper: draw face button with proper 3D frame                      */
/* ================================================================== */
static void draw_face_button(HDC hdc, HFONT font, int x, int y, int w, int h,
                             const wchar_t *text, int pressed)
{
    /* Fill background */
    fill_rect_color(hdc, x, y, w, h, CLR_CELL_BG);

    if (pressed) {
        /* Sunken look when pressed */
        draw_sunken_frame(hdc, x, y, w, h);
    } else {
        /* Raised look */
        draw_raised_frame(hdc, x, y, w, h);
    }

    RECT trc = { x + (pressed ? 2 : 0), y + (pressed ? 2 : 0), x + w, y + h };
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, CLR_BLACK);
    DrawTextW(hdc, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

/* ================================================================== */
/*  Helper: draw toolbar button with 3D frame + active state           */
/* ================================================================== */
static void draw_toolbar_button(HDC hdc, HFONT font, int x, int y, int w, int h,
                                const wchar_t *text, int active)
{
    /* Fill with slightly different bg for toolbar */
    fill_rect_color(hdc, x, y, w, h, active ? RGB(185, 185, 185) : CLR_TOOLBAR_BG);

    /* 1px raised frame */
    HPEN penWh  = CreatePen(PS_SOLID, 1, CLR_WHITE);
    HPEN penDk  = CreatePen(PS_SOLID, 1, CLR_DKGRAY);
    HPEN oldPen;

    oldPen = (HPEN)SelectObject(hdc, penWh);
    MoveToEx(hdc, x, y + h - 1, NULL);
    LineTo(hdc, x, y);
    LineTo(hdc, x + w - 1, y);
    SelectObject(hdc, penDk);
    MoveToEx(hdc, x + w - 1, y, NULL);
    LineTo(hdc, x + w - 1, y + h - 1);
    LineTo(hdc, x, y + h - 1);

    SelectObject(hdc, oldPen);
    DeleteObject(penWh);
    DeleteObject(penDk);

    /* Separator line on right edge */
    HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    oldPen = (HPEN)SelectObject(hdc, sepPen);
    MoveToEx(hdc, x + w - 1, y + 3, NULL);
    LineTo(hdc, x + w - 1, y + h - 3);
    SelectObject(hdc, oldPen);
    DeleteObject(sepPen);

    RECT trc = { x, y, x + w, y + h };
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, active ? RGB(0, 0, 160) : CLR_BLACK);
    DrawTextW(hdc, text, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

/* ================================================================== */
/*  PUBLIC API                                                         */
/* ================================================================== */

Renderer *render_create(HWND hwnd, int w, int h)
{
    Renderer *r = (Renderer *)calloc(1, sizeof(Renderer));
    if (!r) return NULL;

    HDC screen_dc = GetDC(hwnd);
    r->mem_dc  = CreateCompatibleDC(screen_dc);
    r->mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    r->old_bmp = (HBITMAP)SelectObject(r->mem_dc, r->mem_bmp);
    r->buf_w   = w;
    r->buf_h   = h;
    ReleaseDC(hwnd, screen_dc);

    /* Font for cell numbers -- bold, sized to fill 30px cells */
    r->font_num = CreateFontW(
        -20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    /* Font for LED display -- large bold red digits */
    r->font_header = CreateFontW(
        -24, -11, 0, 0, FW_EXTRABOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    /* Font for toolbar buttons -- Chinese-capable font */
    r->font_toolbar = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");

    return r;
}

void render_destroy(Renderer *r)
{
    if (!r) return;
    if (r->mem_dc) {
        SelectObject(r->mem_dc, r->old_bmp);
        DeleteDC(r->mem_dc);
    }
    if (r->mem_bmp)     DeleteObject(r->mem_bmp);
    if (r->font_num)    DeleteObject(r->font_num);
    if (r->font_header) DeleteObject(r->font_header);
    if (r->font_toolbar)DeleteObject(r->font_toolbar);
    free(r);
}

void render_resize(Renderer *r, HWND hwnd, int w, int h)
{
    if (!r) return;
    if (w == r->buf_w && h == r->buf_h) return;

    SelectObject(r->mem_dc, r->old_bmp);
    DeleteObject(r->mem_bmp);

    HDC screen_dc = GetDC(hwnd);
    r->mem_bmp = CreateCompatibleBitmap(screen_dc, w, h);
    r->old_bmp = (HBITMAP)SelectObject(r->mem_dc, r->mem_bmp);
    r->buf_w   = w;
    r->buf_h   = h;
    ReleaseDC(hwnd, screen_dc);
}

/* ------------------------------------------------------------------ */
/*  render_paint                                                       */
/* ------------------------------------------------------------------ */
void render_paint(Renderer *r, HWND hwnd, Game *g, double *prob_map)
{
    if (!r || !g || !g->board) return;

    Board *b   = g->board;
    HDC    hdc = r->mem_dc;
    int    bw  = b->width;
    int    bh  = b->height;

    /* Grid origin */
    int gx = BORDER_SIZE;
    int gy = HEADER_HEIGHT;
    int grid_w = bw * CELL_SIZE;
    int grid_h = bh * CELL_SIZE;
    int client_w = r->buf_w;

    /* ============================================================== */
    /*  1. Fill entire background with classic Win gray                */
    /* ============================================================== */
    fill_rect_color(hdc, 0, 0, r->buf_w, r->buf_h, CLR_FACE_BG);

    /* ============================================================== */
    /*  2. Sunken frame around header area                             */
    /* ============================================================== */
    {
        int hdr_x = BORDER_SIZE / 2;
        int hdr_y = 4;
        int hdr_w = client_w - BORDER_SIZE;
        int hdr_h = HEADER_HEIGHT - 8;
        draw_sunken_frame(hdc, hdr_x, hdr_y, hdr_w, hdr_h);
    }

    /* ============================================================== */
    /*  3. Header bar: LED mine counter, face button, LED timer        */
    /* ============================================================== */
    {
        int led_w = 68;
        int led_h = 34;
        int led_y = (HEADER_HEIGHT - led_h) / 2;

        /* Left: mine counter */
        int mine_display = b->mine_count - b->flagged_count;
        draw_led_display(hdc, r->font_header, BORDER_SIZE + 2, led_y, led_w, led_h,
                         mine_display);

        /* Right: timer */
        draw_led_display(hdc, r->font_header, client_w - BORDER_SIZE - led_w - 2,
                         led_y, led_w, led_h, g->elapsed_seconds);

        /* Center: face button */
        int face_sz = 40;
        int face_x  = (client_w - face_sz) / 2;
        int face_y  = (HEADER_HEIGHT - face_sz) / 2;

        /* Choose face text based on game state + press state */
        const wchar_t *face;
        switch (b->state) {
            case STATE_LOST:    face = L"X-(";  break;
            case STATE_WON:     face = L"B-)";  break;
            default:
                face = g->press_active ? L":-O" : L":-)";
                break;
        }

        draw_face_button(hdc, r->font_header, face_x, face_y, face_sz, face_sz,
                         face, 0);
    }

    /* ============================================================== */
    /*  4. Sunken frame around the minefield                           */
    /* ============================================================== */
    {
        int frame_x = gx - 3;
        int frame_y = gy - 3;
        int frame_w = grid_w + 6;
        int frame_h = grid_h + 6;
        draw_sunken_frame(hdc, frame_x, frame_y, frame_w, frame_h);
    }

    /* ============================================================== */
    /*  5. Mine field cells                                            */
    /* ============================================================== */
    for (int cy = 0; cy < bh; cy++) {
        for (int cx = 0; cx < bw; cx++) {
            Cell *c     = board_cell(b, cx, cy);
            int   px    = gx + cx * CELL_SIZE;
            int   py    = gy + cy * CELL_SIZE;
            int   is_rev  = (c->flags & CELL_REVEALED);
            int   is_mine = (c->flags & CELL_MINE);
            int   is_flag = (c->flags & CELL_FLAGGED);
            int   is_q    = (c->flags & CELL_QUESTION);

            /* --- Fog of war check --- */
            if (g->fog_mode && !is_rev) {
                if (!is_visible_in_fog(b, cx, cy, g->fog_radius)) {
                    fill_rect_color(hdc, px, py, CELL_SIZE, CELL_SIZE, CLR_FOG);
                    continue;
                }
            }

            /* --- Chord press visual: unrevealed+unflagged cells in
                   3x3 area around press point get pressed look --- */
            int chord_pressed = 0;
            if (!is_rev && !is_flag && !is_q && is_in_chord_press(g, cx, cy)) {
                chord_pressed = 1;
            }

            if (is_rev) {
                /* ---- Revealed cell ---- */

                /* Check if this is the exploded mine */
                if (is_mine && b->state == STATE_LOST &&
                    cx == g->explode_x && cy == g->explode_y) {
                    /* RED background for the mine the player clicked */
                    draw_cell_revealed(hdc, px, py, CELL_SIZE, CLR_EXPLODE_BG);
                    draw_mine(hdc, px, py, CELL_SIZE);
                }
                else if (is_mine) {
                    /* Other revealed mines on loss: gray bg + mine */
                    draw_cell_revealed(hdc, px, py, CELL_SIZE, CLR_CELL_REV);
                    draw_mine(hdc, px, py, CELL_SIZE);
                }
                else if (c->number > 0 && c->number <= 8) {
                    /* Numbered cell */
                    draw_cell_revealed(hdc, px, py, CELL_SIZE, CLR_CELL_REV);
                    wchar_t num_str[2] = { L'0' + c->number, L'\0' };
                    RECT nrc = { px, py, px + CELL_SIZE, py + CELL_SIZE };
                    HFONT oldFont = (HFONT)SelectObject(hdc, r->font_num);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, NUM_COLORS[c->number]);
                    DrawTextW(hdc, num_str, 1, &nrc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                }
                else {
                    /* Empty revealed cell (number == 0) */
                    draw_cell_revealed(hdc, px, py, CELL_SIZE, CLR_CELL_REV);
                }
            }
            else {
                /* ---- Unrevealed cell ---- */

                /* On loss: wrong flags and unrevealed mines */
                if (b->state == STATE_LOST) {
                    if (is_flag && !is_mine) {
                        /* Wrong flag: raised cell + flag + red X */
                        draw_cell_raised(hdc, px, py, CELL_SIZE);
                        draw_flag(hdc, px, py, CELL_SIZE);
                        draw_x(hdc, px, py, CELL_SIZE);
                        goto next_cell;
                    }
                    if (is_mine && !is_flag) {
                        /* Unrevealed mine on loss: show it flat */
                        draw_cell_revealed(hdc, px, py, CELL_SIZE, CLR_CELL_REV);
                        draw_mine(hdc, px, py, CELL_SIZE);
                        goto next_cell;
                    }
                }

                if (chord_pressed) {
                    /* Chord press: draw as pressed/sunken */
                    draw_cell_pressed(hdc, px, py, CELL_SIZE);
                }
                else if (is_flag) {
                    /* Flagged cell */
                    draw_cell_raised(hdc, px, py, CELL_SIZE);
                    draw_flag(hdc, px, py, CELL_SIZE);
                }
                else if (is_q) {
                    /* Question mark cell */
                    draw_cell_raised(hdc, px, py, CELL_SIZE);
                    RECT qrc = { px, py, px + CELL_SIZE, py + CELL_SIZE };
                    HFONT oldFont = (HFONT)SelectObject(hdc, r->font_num);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(128, 0, 128));
                    DrawTextW(hdc, L"?", 1, &qrc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                }
                else {
                    /* Plain unrevealed */
                    draw_cell_raised(hdc, px, py, CELL_SIZE);
                }

                /* --- Heat map overlay --- */
                if (g->heatmap_on && prob_map && !is_flag && !chord_pressed) {
                    double prob = prob_map[cy * bw + cx];
                    if (prob >= 0.0) {
                        COLORREF hc = heatmap_color(prob);
                        HBRUSH brHeat = CreateSolidBrush(hc);
                        int pad = 4;
                        RECT hrc = { px + pad, py + pad,
                                     px + CELL_SIZE - pad, py + CELL_SIZE - pad };
                        FillRect(hdc, &hrc, brHeat);
                        DeleteObject(brHeat);

                        /* Probability text */
                        wchar_t prob_str[8];
                        int pct = (int)(prob * 100 + 0.5);
                        if (pct > 99) pct = 99;
                        _snwprintf(prob_str, 8, L".%02d", pct);

                        HFONT smallFont = CreateFontW(
                            -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Consolas");
                        HFONT oldFont = (HFONT)SelectObject(hdc, smallFont);
                        SetBkMode(hdc, TRANSPARENT);
                        SetTextColor(hdc, CLR_BLACK);
                        RECT trc = { px, py + 2, px + CELL_SIZE, py + CELL_SIZE };
                        DrawTextW(hdc, prob_str, -1, &trc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        SelectObject(hdc, oldFont);
                        DeleteObject(smallFont);
                    }
                }
            }

            /* --- Hint highlight --- */
            if (g->hint_active && cx == g->hint_x && cy == g->hint_y) {
                HPEN greenPen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
                HPEN oldPen = (HPEN)SelectObject(hdc, greenPen);
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, px + 1, py + 1, px + CELL_SIZE - 1, py + CELL_SIZE - 1);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBr);
                DeleteObject(greenPen);
            }

            next_cell:;
        }
    }

    /* ============================================================== */
    /*  6. Opponent progress bar (placeholder: uses replay_on flag)    */
    /*     When wired up to NetState, draw a progress bar showing      */
    /*     opponent's revealed_count / total non-mine cells.           */
    /* ============================================================== */
    if (g->replay_on) {
        int bar_x = BORDER_SIZE;
        int bar_y = gy + grid_h + 1;
        int bar_w = grid_w;
        int bar_h = 5;

        /* Background (dark) */
        fill_rect_color(hdc, bar_x, bar_y, bar_w, bar_h, RGB(60, 60, 60));

        /* Progress fill (placeholder: 50% for now, replace with
           actual opponent->revealed_count / total when NetState is wired) */
        int total_safe = bw * bh - b->mine_count;
        int fill_w = 0;
        if (total_safe > 0) {
            /* Placeholder: show own progress as demo */
            fill_w = (int)((double)b->revealed_count / total_safe * bar_w);
            if (fill_w > bar_w) fill_w = bar_w;
        }
        fill_rect_color(hdc, bar_x, bar_y, fill_w, bar_h, RGB(0, 200, 80));

        /* Tiny sunken border */
        HPEN penDk = CreatePen(PS_SOLID, 1, CLR_DKGRAY);
        HPEN penWh = CreatePen(PS_SOLID, 1, CLR_WHITE);
        HPEN oldPen;
        oldPen = (HPEN)SelectObject(hdc, penDk);
        MoveToEx(hdc, bar_x - 1, bar_y + bar_h, NULL);
        LineTo(hdc, bar_x - 1, bar_y - 1);
        LineTo(hdc, bar_x + bar_w, bar_y - 1);
        SelectObject(hdc, penWh);
        MoveToEx(hdc, bar_x + bar_w, bar_y - 1, NULL);
        LineTo(hdc, bar_x + bar_w, bar_y + bar_h);
        LineTo(hdc, bar_x - 1, bar_y + bar_h);
        SelectObject(hdc, oldPen);
        DeleteObject(penDk);
        DeleteObject(penWh);
    }

    /* ============================================================== */
    /*  7. Toolbar (4 Chinese buttons with 3D look)                    */
    /* ============================================================== */
    {
        int toolbar_y = HEADER_HEIGHT + bh * CELL_SIZE + BORDER_SIZE;

        static const wchar_t *btn_labels[4] = {
            L"\x63D0\x793A(H)",       /* 提示(H)     */
            L"\x70ED\x529B\x56FE(P)", /* 热力图(P)   */
            L"\x96FE\x6A21\x5F0F(F)", /* 雾模式(F)   */
            L"\x65B0\x6E38\x620F(N)"  /* 新游戏(N)   */
        };

        /* Active state for toggle buttons */
        int btn_active[4] = {
            g->hint_active,     /* hint toggle */
            g->heatmap_on,      /* heatmap toggle */
            g->fog_mode,        /* fog toggle */
            0                   /* new game -- never stays "active" */
        };

        int btn_w = client_w / 4;
        int btn_h = TOOLBAR_HEIGHT;

        for (int i = 0; i < 4; i++) {
            int bx = i * btn_w;
            int by = toolbar_y;
            int bw_actual = (i == 3) ? (client_w - bx) : btn_w;
            draw_toolbar_button(hdc, r->font_toolbar, bx, by, bw_actual, btn_h,
                                btn_labels[i], btn_active[i]);
        }
    }

    /* ============================================================== */
    /*  8. Blit to screen                                              */
    /* ============================================================== */
    {
        PAINTSTRUCT ps;
        HDC screen_dc = BeginPaint(hwnd, &ps);
        BitBlt(screen_dc, 0, 0, r->buf_w, r->buf_h, hdc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
    }
}

/* ------------------------------------------------------------------ */
/*  render_calc_window_size                                            */
/* ------------------------------------------------------------------ */
void render_calc_window_size(int board_w, int board_h,
                             int *client_w, int *client_h)
{
    *client_w = 2 * BORDER_SIZE + board_w * CELL_SIZE;
    *client_h = HEADER_HEIGHT + board_h * CELL_SIZE + BORDER_SIZE + TOOLBAR_HEIGHT;
}

/* ------------------------------------------------------------------ */
/*  render_pixel_to_cell                                               */
/* ------------------------------------------------------------------ */
int render_pixel_to_cell(int board_w, int board_h,
                         int px, int py, int *cx, int *cy)
{
    int gx = BORDER_SIZE;
    int gy_local = HEADER_HEIGHT;

    int x = px - gx;
    int y = py - gy_local;

    if (x < 0 || y < 0) return 0;

    *cx = x / CELL_SIZE;
    *cy = y / CELL_SIZE;

    if (*cx < 0 || *cx >= board_w) return 0;
    if (*cy < 0 || *cy >= board_h) return 0;

    return 1;
}

/* ------------------------------------------------------------------ */
/*  render_toolbar_hit                                                 */
/* ------------------------------------------------------------------ */
int render_toolbar_hit(int board_w, int board_h, int client_w,
                       int px, int py)
{
    (void)board_w;

    int toolbar_y = HEADER_HEIGHT + board_h * CELL_SIZE + BORDER_SIZE;
    int toolbar_bottom = toolbar_y + TOOLBAR_HEIGHT;

    if (py < toolbar_y || py >= toolbar_bottom) return -1;
    if (px < 0 || px >= client_w) return -1;

    int btn_w = client_w / 4;
    if (btn_w <= 0) return -1;

    int idx = px / btn_w;
    if (idx > 3) idx = 3;

    return idx;
}

/* ------------------------------------------------------------------ */
/*  render_face_hit                                                    */
/* ------------------------------------------------------------------ */
int render_face_hit(int board_w, int client_w, int px, int py)
{
    (void)board_w;

    int face_sz = 40;
    int face_x  = (client_w - face_sz) / 2;
    int face_y  = (HEADER_HEIGHT - face_sz) / 2;

    if (px >= face_x && px < face_x + face_sz &&
        py >= face_y && py < face_y + face_sz) {
        return 1;
    }
    return 0;
}
