/*
 * render.c  --  Win32 GDI rendering for Minesweeper
 *
 * Windows 7 Minesweeper style: 3D sunken frames, proper cell shading,
 * chord press visual feedback, exploded mine, LED counters, face button.
 *
 * All text is wide-character (wchar_t / L"...") for Unicode/Chinese support.
 * Uses W-suffix Win32 APIs throughout.
 *
 * Supports 4 colour themes selectable at runtime.
 */

#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Theme definitions (4 built-in themes)                              */
/* ------------------------------------------------------------------ */
static const ColorTheme THEMES[4] = {
    { /* Classic (Windows 7 gray) */
        RGB(192,192,192), RGB(192,192,192), RGB(200,200,200), RGB(180,180,180),
        RGB(255,255,255), RGB(224,224,224), RGB(128,128,128), RGB(64,64,64),
        RGB(16,16,16), RGB(255,0,0),
        RGB(192,192,192), RGB(185,185,185),
        RGB(32,32,32), RGB(255,0,0), RGB(0,0,0), RGB(255,0,0), RGB(255,200,0),
        { 0, RGB(0,0,255), RGB(0,128,0), RGB(255,0,0), RGB(0,0,128),
          RGB(128,0,0), RGB(0,128,128), RGB(0,0,0), RGB(128,128,128) }
    },
    { /* Dark Mode */
        RGB(40,40,45), RGB(55,55,60), RGB(70,70,75), RGB(45,45,50),
        RGB(90,90,95), RGB(75,75,80), RGB(30,30,35), RGB(15,15,20),
        RGB(10,10,12), RGB(0,200,255),
        RGB(50,50,55), RGB(40,40,45),
        RGB(20,20,22), RGB(180,30,30), RGB(200,200,200), RGB(255,60,60), RGB(0,200,255),
        { 0, RGB(80,140,255), RGB(80,200,80), RGB(255,80,80), RGB(100,100,255),
          RGB(200,80,80), RGB(80,200,200), RGB(200,200,200), RGB(140,140,140) }
    },
    { /* Ocean */
        RGB(180,210,230), RGB(160,195,220), RGB(190,220,240), RGB(140,175,200),
        RGB(230,240,250), RGB(200,225,240), RGB(100,140,170), RGB(50,80,110),
        RGB(20,40,60), RGB(0,255,200),
        RGB(150,190,215), RGB(130,170,195),
        RGB(30,50,70), RGB(255,80,80), RGB(30,60,90), RGB(255,100,100), RGB(255,220,50),
        { 0, RGB(0,60,200), RGB(0,150,80), RGB(220,50,50), RGB(0,40,160),
          RGB(160,40,40), RGB(0,140,140), RGB(30,30,30), RGB(100,130,160) }
    },
    { /* Retro Pixel */
        RGB(200,180,140), RGB(180,160,120), RGB(210,195,165), RGB(160,140,100),
        RGB(240,230,200), RGB(220,210,180), RGB(120,100,70), RGB(60,50,30),
        RGB(30,20,10), RGB(255,180,0),
        RGB(170,150,110), RGB(150,130,90),
        RGB(40,30,20), RGB(255,50,0), RGB(40,30,20), RGB(255,50,0), RGB(255,255,0),
        { 0, RGB(0,0,200), RGB(0,150,0), RGB(200,0,0), RGB(0,0,120),
          RGB(150,0,0), RGB(0,120,120), RGB(0,0,0), RGB(100,80,60) }
    },
};
static int g_theme = 0;

/* ================================================================== */
/*  Theme accessor functions                                           */
/* ================================================================== */
void render_set_theme(Renderer *r, int index) {
    if (index >= 0 && index < THEME_COUNT) {
        r->theme_index = index;
        g_theme = index;
    }
}
const ColorTheme *render_get_theme(Renderer *r) {
    return &THEMES[r->theme_index];
}

/* ================================================================== */
/*  Helper: draw a sunken frame (Win classic inset border)             */
/*  Draws a 2px border: dark-gray/black on top-left, white on         */
/*  bottom-right. The interior is NOT filled.                          */
/* ================================================================== */
static void draw_sunken_frame(HDC hdc, int x, int y, int w, int h)
{
    HPEN penDk   = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_dkgray);
    HPEN penBlk  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_black);
    HPEN penWh   = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
    HPEN penLt   = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_ltgray);
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
    HPEN penWh  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
    HPEN penLt  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_ltgray);
    HPEN penDk  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_dkgray);
    HPEN penBlk = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_black);
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
    HPEN penWh  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
    HPEN penDk  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_dkgray);
    HPEN oldPen;

    /* Gradient fill: subtle top-to-bottom darkening (4 strips) */
    {
        COLORREF base = THEMES[g_theme].cell_raised;
        int r0 = GetRValue(base), g0 = GetGValue(base), b0 = GetBValue(base);
        int strip_h = sz / 4;
        for (int s = 0; s < 4; s++) {
            int darken = s * 4;  /* subtle: 0, 4, 8, 12 */
            int sr = r0 - darken > 0 ? r0 - darken : 0;
            int sg = g0 - darken > 0 ? g0 - darken : 0;
            int sb = b0 - darken > 0 ? b0 - darken : 0;
            HBRUSH br = CreateSolidBrush(RGB(sr, sg, sb));
            RECT strip = { x, y + s*strip_h, x+sz, y + (s == 3 ? sz : (s+1)*strip_h) };
            FillRect(hdc, &strip, br);
            DeleteObject(br);
        }
    }

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
    fill_rect_color(hdc, x, y, sz, sz, THEMES[g_theme].cell_pressed);

    /* 1px dark border on top-left, giving a sunken look */
    HPEN penDk = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_dkgray);
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

    /* Very subtle 1px border (slightly darker than fill) --
       use a blend between border_dkgray and border_ltgray */
    COLORREF border_clr = THEMES[g_theme].border_dkgray;
    /* For classic theme, use the original subtle gray */
    int r_val = (int)(GetRValue(THEMES[g_theme].cell_revealed) * 0.875);
    int g_val = (int)(GetGValue(THEMES[g_theme].cell_revealed) * 0.875);
    int b_val = (int)(GetBValue(THEMES[g_theme].cell_revealed) * 0.875);
    border_clr = RGB(r_val, g_val, b_val);

    HPEN pen = CreatePen(PS_SOLID, 1, border_clr);
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
    HPEN polePen = CreatePen(PS_SOLID, 2, THEMES[g_theme].mine_color);
    HPEN oldPen = (HPEN)SelectObject(hdc, polePen);
    MoveToEx(hdc, mx, top, NULL);
    LineTo(hdc, mx, bot);
    SelectObject(hdc, oldPen);
    DeleteObject(polePen);

    /* Base line */
    HPEN basePen = CreatePen(PS_SOLID, 2, THEMES[g_theme].mine_color);
    oldPen = (HPEN)SelectObject(hdc, basePen);
    MoveToEx(hdc, mx - 5, bot, NULL);
    LineTo(hdc, mx + 6, bot);
    /* Small foot */
    MoveToEx(hdc, mx - 3, bot + 2, NULL);
    LineTo(hdc, mx + 4, bot + 2);
    SelectObject(hdc, oldPen);
    DeleteObject(basePen);

    /* Red triangle flag */
    HBRUSH redBr = CreateSolidBrush(THEMES[g_theme].flag_color);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, redBr);
    COLORREF flag_outline = THEMES[g_theme].flag_color;
    /* Slightly darker outline */
    int fr = GetRValue(flag_outline); if (fr > 55) fr -= 55; else fr = 0;
    int fg = GetGValue(flag_outline); if (fg > 55) fg -= 55; else fg = 0;
    int fb = GetBValue(flag_outline); if (fb > 55) fb -= 55; else fb = 0;
    HPEN flagPen = CreatePen(PS_SOLID, 1, RGB(fr, fg, fb));
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
/*  Helper: draw mine - classic style (black circle with spikes)       */
/* ================================================================== */
static void draw_mine_classic(HDC hdc, int cx, int cy, int sz, COLORREF mc)
{
    int midx = cx + sz / 2;
    int midy = cy + sz / 2;
    int rad  = sz / 2 - 5;

    /* Spikes (cross lines) */
    HPEN spkPen = CreatePen(PS_SOLID, 2, mc);
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
    HBRUSH br = CreateSolidBrush(mc);
    HPEN   pen = CreatePen(PS_SOLID, 1, mc);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    oldPen = (HPEN)SelectObject(hdc, pen);
    Ellipse(hdc, midx - rad, midy - rad, midx + rad, midy + rad);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);

    /* Specular highlight (small white dot in upper-left) */
    HBRUSH wh = CreateSolidBrush(THEMES[g_theme].border_white);
    HPEN wpen = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
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
/*  Helper: draw mine - skull style                                    */
/* ================================================================== */
static void draw_mine_skull(HDC hdc, int cx, int cy, int sz, COLORREF mc)
{
    int midx = cx + sz / 2;
    int midy = cy + sz / 2;
    int rad  = sz / 2 - 6;

    HBRUSH br = CreateSolidBrush(mc);
    HPEN   pen = CreatePen(PS_SOLID, 1, mc);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN   oldPen = (HPEN)SelectObject(hdc, pen);

    /* Round head */
    Ellipse(hdc, midx - rad, midy - rad - 1, midx + rad, midy + rad - 3);

    /* Two small filled circles for eyes */
    HBRUSH eyeBr = CreateSolidBrush(THEMES[g_theme].cell_revealed);
    HPEN   eyePen = CreatePen(PS_SOLID, 1, THEMES[g_theme].cell_revealed);
    SelectObject(hdc, eyeBr);
    SelectObject(hdc, eyePen);
    int er = 2;
    Ellipse(hdc, midx - 4 - er, midy - 3 - er, midx - 4 + er, midy - 3 + er);
    Ellipse(hdc, midx + 4 - er, midy - 3 - er, midx + 4 + er, midy - 3 + er);

    /* Mouth: horizontal line */
    SelectObject(hdc, pen);
    MoveToEx(hdc, midx - 4, midy + 2, NULL);
    LineTo(hdc, midx + 5, midy + 2);

    /* Jaw / chin vertical lines */
    MoveToEx(hdc, midx - 2, midy + 2, NULL);
    LineTo(hdc, midx - 2, midy + 5);
    MoveToEx(hdc, midx + 2, midy + 2, NULL);
    LineTo(hdc, midx + 2, midy + 5);

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
    DeleteObject(eyeBr);
    DeleteObject(eyePen);
}

/* ================================================================== */
/*  Helper: draw mine - bomb with fuse style                           */
/* ================================================================== */
static void draw_mine_bomb(HDC hdc, int cx, int cy, int sz, COLORREF mc)
{
    int midx = cx + sz / 2;
    int midy = cy + sz / 2 + 2;
    int rad  = sz / 2 - 7;

    /* Main body (filled circle) */
    HBRUSH br = CreateSolidBrush(mc);
    HPEN   pen = CreatePen(PS_SOLID, 1, mc);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN   oldPen = (HPEN)SelectObject(hdc, pen);
    Ellipse(hdc, midx - rad, midy - rad, midx + rad, midy + rad);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);

    /* Fuse: arc from top-right */
    HPEN fusePen = CreatePen(PS_SOLID, 2, mc);
    oldPen = (HPEN)SelectObject(hdc, fusePen);
    int fx = midx + rad - 2;
    int fy = midy - rad;
    MoveToEx(hdc, fx, fy, NULL);
    LineTo(hdc, fx + 3, fy - 4);
    LineTo(hdc, fx + 1, fy - 7);
    SelectObject(hdc, oldPen);
    DeleteObject(fusePen);

    /* Sparks: small dots at fuse tip */
    COLORREF spark = RGB(255, 200, 0);
    HBRUSH sparkBr = CreateSolidBrush(spark);
    HPEN sparkPen = CreatePen(PS_SOLID, 1, spark);
    oldBr = (HBRUSH)SelectObject(hdc, sparkBr);
    oldPen = (HPEN)SelectObject(hdc, sparkPen);
    Ellipse(hdc, fx, fy - 9, fx + 3, fy - 6);
    Ellipse(hdc, fx + 3, fy - 10, fx + 5, fy - 8);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(sparkBr);
    DeleteObject(sparkPen);

    /* Specular highlight */
    HBRUSH wh = CreateSolidBrush(THEMES[g_theme].border_white);
    HPEN wpen = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
    oldBr = (HBRUSH)SelectObject(hdc, wh);
    oldPen = (HPEN)SelectObject(hdc, wpen);
    Ellipse(hdc, midx - rad/2 - 1, midy - rad/2 - 1,
                 midx - rad/2 + 2, midy - rad/2 + 2);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(wh);
    DeleteObject(wpen);
}

/* ================================================================== */
/*  Helper: draw mine - radiation symbol style                         */
/* ================================================================== */
static void draw_mine_radiation(HDC hdc, int cx, int cy, int sz, COLORREF mc)
{
    int midx = cx + sz / 2;
    int midy = cy + sz / 2;
    int rad  = sz / 2 - 5;

    HBRUSH br = CreateSolidBrush(mc);
    HPEN   pen = CreatePen(PS_SOLID, 1, mc);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN   oldPen = (HPEN)SelectObject(hdc, pen);

    /* Three pie/fan sectors at 120-degree intervals */
    int inner_r = rad / 3;
    /* Sector 1: top (270 +/- 30 degrees) */
    Pie(hdc, midx - rad, midy - rad, midx + rad, midy + rad,
        midx + (int)(rad * cos(-60.0 * 3.14159265/180.0)),
        midy + (int)(rad * sin(-60.0 * 3.14159265/180.0)),
        midx + (int)(rad * cos(-120.0 * 3.14159265/180.0)),
        midy + (int)(rad * sin(-120.0 * 3.14159265/180.0)));
    /* Sector 2: bottom-right (30 +/- 30) */
    Pie(hdc, midx - rad, midy - rad, midx + rad, midy + rad,
        midx + (int)(rad * cos(60.0 * 3.14159265/180.0)),
        midy + (int)(rad * sin(60.0 * 3.14159265/180.0)),
        midx + (int)(rad * cos(0.0 * 3.14159265/180.0)),
        midy + (int)(rad * sin(0.0 * 3.14159265/180.0)));
    /* Sector 3: bottom-left (150 +/- 30) */
    Pie(hdc, midx - rad, midy - rad, midx + rad, midy + rad,
        midx + (int)(rad * cos(180.0 * 3.14159265/180.0)),
        midy + (int)(rad * sin(180.0 * 3.14159265/180.0)),
        midx + (int)(rad * cos(120.0 * 3.14159265/180.0)),
        midy + (int)(rad * sin(120.0 * 3.14159265/180.0)));

    /* Center circle (hole) in background colour */
    HBRUSH bgBr = CreateSolidBrush(THEMES[g_theme].cell_revealed);
    HPEN bgPen = CreatePen(PS_SOLID, 1, THEMES[g_theme].cell_revealed);
    SelectObject(hdc, bgBr);
    SelectObject(hdc, bgPen);
    Ellipse(hdc, midx - inner_r, midy - inner_r, midx + inner_r, midy + inner_r);

    /* Small center dot in mine colour */
    SelectObject(hdc, br);
    SelectObject(hdc, pen);
    int dot_r = 2;
    Ellipse(hdc, midx - dot_r, midy - dot_r, midx + dot_r, midy + dot_r);

    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
    DeleteObject(bgBr);
    DeleteObject(bgPen);
}

/* ================================================================== */
/*  Helper: draw mine - 8-point star style                             */
/* ================================================================== */
static void draw_mine_star(HDC hdc, int cx, int cy, int sz, COLORREF mc)
{
    int midx = cx + sz / 2;
    int midy = cy + sz / 2;
    int outer_r = sz / 2 - 5;
    int inner_r = outer_r / 2;
    double pi = 3.14159265358979;

    POINT pts[16];
    for (int i = 0; i < 16; i++) {
        double angle = pi / 2.0 + i * (2.0 * pi / 16.0);
        int rr = (i % 2 == 0) ? outer_r : inner_r;
        pts[i].x = midx + (int)(rr * cos(angle));
        pts[i].y = midy - (int)(rr * sin(angle));
    }

    HBRUSH br = CreateSolidBrush(mc);
    HPEN   pen = CreatePen(PS_SOLID, 1, mc);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, br);
    HPEN   oldPen = (HPEN)SelectObject(hdc, pen);
    Polygon(hdc, pts, 16);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);

    /* Small specular highlight */
    HBRUSH wh = CreateSolidBrush(THEMES[g_theme].border_white);
    HPEN wpen = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
    oldBr = (HBRUSH)SelectObject(hdc, wh);
    oldPen = (HPEN)SelectObject(hdc, wpen);
    Ellipse(hdc, midx - 3, midy - 3, midx, midy);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(wh);
    DeleteObject(wpen);
}

/* ================================================================== */
/*  Mine icon dispatcher: selects style from Renderer mine_icon field  */
/* ================================================================== */
static void draw_mine_icon(HDC hdc, Renderer *r, int cx, int cy, int sz)
{
    COLORREF mc = THEMES[g_theme].mine_color;
    int pad = 5;

    if (r->mine_icon == 5 && r->custom_mine_bmp) {
        /* Custom BMP: StretchBlt from custom_mine_dc */
        StretchBlt(hdc, cx+pad, cy+pad, sz-2*pad, sz-2*pad,
                   r->custom_mine_dc, 0, 0,
                   sz-2*pad, sz-2*pad, SRCCOPY);
        return;
    }

    switch (r->mine_icon) {
    case 0: /* Classic: circle + cross spikes */
        draw_mine_classic(hdc, cx, cy, sz, mc);
        break;
    case 1: /* Skull */
        draw_mine_skull(hdc, cx, cy, sz, mc);
        break;
    case 2: /* Bomb with fuse */
        draw_mine_bomb(hdc, cx, cy, sz, mc);
        break;
    case 3: /* Radiation symbol */
        draw_mine_radiation(hdc, cx, cy, sz, mc);
        break;
    case 4: /* Star */
        draw_mine_star(hdc, cx, cy, sz, mc);
        break;
    default:
        draw_mine_classic(hdc, cx, cy, sz, mc);
        break;
    }
}

/* ================================================================== */
/*  Helper: draw red X over a cell (for wrong flags on loss)           */
/* ================================================================== */
static void draw_x(HDC hdc, int cx, int cy, int sz)
{
    HPEN pen = CreatePen(PS_SOLID, 2, THEMES[g_theme].flag_color);
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
    HBRUSH brBlack = CreateSolidBrush(THEMES[g_theme].led_bg);
    FillRect(hdc, &inner, brBlack);
    DeleteObject(brBlack);

    /* Clamp display value */
    if (value > 999) value = 999;
    if (value < -99) value = -99;

    wchar_t buf[8];
    _snwprintf(buf, 8, L"%03d", value);

    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, THEMES[g_theme].led_fg);
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
    fill_rect_color(hdc, x, y, w, h, THEMES[g_theme].cell_raised);

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
    SetTextColor(hdc, THEMES[g_theme].mine_color);
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
    fill_rect_color(hdc, x, y, w, h, active ? THEMES[g_theme].toolbar_active : THEMES[g_theme].toolbar_bg);

    /* 1px raised frame */
    HPEN penWh  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
    HPEN penDk  = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_dkgray);
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
    COLORREF sep_clr = THEMES[g_theme].border_dkgray;
    HPEN sepPen = CreatePen(PS_SOLID, 1, sep_clr);
    oldPen = (HPEN)SelectObject(hdc, sepPen);
    MoveToEx(hdc, x + w - 1, y + 3, NULL);
    LineTo(hdc, x + w - 1, y + h - 3);
    SelectObject(hdc, oldPen);
    DeleteObject(sepPen);

    RECT trc = { x, y, x + w, y + h };
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    /* Active text uses a highlight colour; inactive uses mine_color (dark text) */
    SetTextColor(hdc, active ? THEMES[g_theme].num_colors[4] : THEMES[g_theme].mine_color);
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

    r->theme_index = 0;
    r->mine_icon = 0;
    r->custom_mine_bmp = NULL;
    r->custom_mine_dc = NULL;

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
    /* Clean up custom mine icon resources */
    if (r->custom_mine_dc)  { DeleteDC(r->custom_mine_dc);  r->custom_mine_dc = NULL; }
    if (r->custom_mine_bmp) { DeleteObject(r->custom_mine_bmp); r->custom_mine_bmp = NULL; }
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
    /*  1. Fill entire background with theme bg                       */
    /* ============================================================== */
    fill_rect_color(hdc, 0, 0, r->buf_w, r->buf_h, THEMES[g_theme].bg);

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
            int   cell_x = gx + cx * CELL_SIZE;
            int   cell_y = gy + cy * CELL_SIZE;
            int   is_rev  = (c->flags & CELL_REVEALED);
            int   is_mine = (c->flags & CELL_MINE);
            int   is_flag = (c->flags & CELL_FLAGGED);
            int   is_q    = (c->flags & CELL_QUESTION);

            /* --- Fog of war check --- */
            if (g->fog_mode && !is_rev) {
                if (!is_visible_in_fog(b, cx, cy, g->fog_radius)) {
                    fill_rect_color(hdc, cell_x, cell_y, CELL_SIZE, CELL_SIZE,
                                    THEMES[g_theme].fog_color);
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
                    draw_cell_revealed(hdc, cell_x, cell_y, CELL_SIZE,
                                       THEMES[g_theme].explode_bg);
                    draw_mine_icon(hdc, r, cell_x, cell_y, CELL_SIZE);
                }
                else if (is_mine) {
                    /* Other revealed mines on loss: gray bg + mine */
                    draw_cell_revealed(hdc, cell_x, cell_y, CELL_SIZE,
                                       THEMES[g_theme].cell_revealed);
                    draw_mine_icon(hdc, r, cell_x, cell_y, CELL_SIZE);
                }
                else if (c->number > 0 && c->number <= 8) {
                    /* Numbered cell */
                    draw_cell_revealed(hdc, cell_x, cell_y, CELL_SIZE,
                                       THEMES[g_theme].cell_revealed);
                    wchar_t num_str[2] = { L'0' + c->number, L'\0' };
                    RECT nrc = { cell_x, cell_y,
                                 cell_x + CELL_SIZE, cell_y + CELL_SIZE };
                    HFONT oldFont = (HFONT)SelectObject(hdc, r->font_num);
                    SetBkMode(hdc, TRANSPARENT);
                    /* Shadow: offset +1,+1 in dark color */
                    {
                        RECT src = { cell_x+1, cell_y+1,
                                     cell_x+CELL_SIZE+1, cell_y+CELL_SIZE+1 };
                        SetTextColor(hdc, RGB(60, 60, 60));
                        DrawTextW(hdc, num_str, 1, &src,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                    /* Normal number on top */
                    SetTextColor(hdc, THEMES[g_theme].num_colors[c->number]);
                    DrawTextW(hdc, num_str, 1, &nrc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                }
                else {
                    /* Empty revealed cell (number == 0) */
                    draw_cell_revealed(hdc, cell_x, cell_y, CELL_SIZE,
                                       THEMES[g_theme].cell_revealed);
                }
            }
            else {
                /* ---- Unrevealed cell ---- */

                /* On loss: wrong flags and unrevealed mines */
                if (b->state == STATE_LOST) {
                    if (is_flag && !is_mine) {
                        /* Wrong flag: raised cell + flag + red X */
                        draw_cell_raised(hdc, cell_x, cell_y, CELL_SIZE);
                        draw_flag(hdc, cell_x, cell_y, CELL_SIZE);
                        draw_x(hdc, cell_x, cell_y, CELL_SIZE);
                        goto next_cell;
                    }
                    if (is_mine && !is_flag) {
                        /* Unrevealed mine on loss: show it flat */
                        draw_cell_revealed(hdc, cell_x, cell_y, CELL_SIZE,
                                           THEMES[g_theme].cell_revealed);
                        draw_mine_icon(hdc, r, cell_x, cell_y, CELL_SIZE);
                        goto next_cell;
                    }
                }

                if (chord_pressed) {
                    /* Chord press: draw as pressed/sunken */
                    draw_cell_pressed(hdc, cell_x, cell_y, CELL_SIZE);
                }
                else if (is_flag) {
                    /* Flagged cell */
                    draw_cell_raised(hdc, cell_x, cell_y, CELL_SIZE);
                    draw_flag(hdc, cell_x, cell_y, CELL_SIZE);
                }
                else if (is_q) {
                    /* Question mark cell */
                    draw_cell_raised(hdc, cell_x, cell_y, CELL_SIZE);
                    RECT qrc = { cell_x, cell_y,
                                 cell_x + CELL_SIZE, cell_y + CELL_SIZE };
                    HFONT oldFont = (HFONT)SelectObject(hdc, r->font_num);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(128, 0, 128));
                    DrawTextW(hdc, L"?", 1, &qrc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, oldFont);
                }
                else {
                    /* Plain unrevealed */
                    draw_cell_raised(hdc, cell_x, cell_y, CELL_SIZE);
                }

                /* --- Heat map overlay --- */
                if (g->heatmap_on && prob_map && !is_flag && !chord_pressed) {
                    double prob = prob_map[cy * bw + cx];
                    if (prob >= 0.0) {
                        COLORREF hc = heatmap_color(prob);
                        HBRUSH brHeat = CreateSolidBrush(hc);
                        int pad = 4;
                        RECT hrc = { cell_x + pad, cell_y + pad,
                                     cell_x + CELL_SIZE - pad,
                                     cell_y + CELL_SIZE - pad };
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
                        SetTextColor(hdc, THEMES[g_theme].mine_color);
                        RECT trc = { cell_x, cell_y + 2,
                                     cell_x + CELL_SIZE, cell_y + CELL_SIZE };
                        DrawTextW(hdc, prob_str, -1, &trc,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        SelectObject(hdc, oldFont);
                        DeleteObject(smallFont);
                    }
                }
            }

            /* --- Mouse hover highlight on unrevealed cells --- */
            if (cx == g->hover_x && cy == g->hover_y && !is_rev && !g->press_active) {
                HPEN hp = CreatePen(PS_SOLID, 1, RGB(100, 180, 255));
                HPEN old_hp = (HPEN)SelectObject(hdc, hp);
                HBRUSH old_hb = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, cell_x+1, cell_y+1,
                          cell_x+CELL_SIZE-1, cell_y+CELL_SIZE-1);
                SelectObject(hdc, old_hb);
                SelectObject(hdc, old_hp);
                DeleteObject(hp);
            }

            /* --- Hint highlight --- */
            if (g->hint_active && cx == g->hint_x && cy == g->hint_y) {
                HPEN greenPen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
                HPEN oldPen = (HPEN)SelectObject(hdc, greenPen);
                HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, cell_x + 1, cell_y + 1,
                          cell_x + CELL_SIZE - 1, cell_y + CELL_SIZE - 1);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBr);
                DeleteObject(greenPen);
            }

            /* --- Cursor highlight (pulsing border) --- */
            if (g->cursor_visible && cx == g->cursor_x && cy == g->cursor_y) {
                int pen_w = (g->cursor_blink % 6 < 3) ? 3 : 2;
                HPEN cp = CreatePen(PS_SOLID, pen_w, THEMES[g_theme].cursor_color);
                HPEN old_p = (HPEN)SelectObject(hdc, cp);
                HBRUSH old_b = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, cell_x + 1, cell_y + 1,
                          cell_x + CELL_SIZE - 1, cell_y + CELL_SIZE - 1);
                SelectObject(hdc, old_b);
                SelectObject(hdc, old_p);
                DeleteObject(cp);
            }

            /* --- Autoplay highlight (blue border) --- */
            if (g->autoplay_active && cx == g->autoplay_hl_x && cy == g->autoplay_hl_y) {
                HPEN ap = CreatePen(PS_SOLID, 3, RGB(0, 120, 255));
                HPEN old_p = (HPEN)SelectObject(hdc, ap);
                HBRUSH old_b = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, cell_x + 1, cell_y + 1,
                          cell_x + CELL_SIZE - 1, cell_y + CELL_SIZE - 1);
                SelectObject(hdc, old_b);
                SelectObject(hdc, old_p);
                DeleteObject(ap);
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
        HPEN penDk = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_dkgray);
        HPEN penWh = CreatePen(PS_SOLID, 1, THEMES[g_theme].border_white);
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

/* ------------------------------------------------------------------ */
/*  render_load_custom_mine                                            */
/* ------------------------------------------------------------------ */
void render_load_custom_mine(Renderer *r, HWND hwnd, const wchar_t *path)
{
    /* Clean up old */
    if (r->custom_mine_dc)  { DeleteDC(r->custom_mine_dc);  r->custom_mine_dc = NULL; }
    if (r->custom_mine_bmp) { DeleteObject(r->custom_mine_bmp); r->custom_mine_bmp = NULL; }

    r->custom_mine_bmp = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP,
                                              0, 0, LR_LOADFROMFILE);
    if (r->custom_mine_bmp) {
        HDC screen = GetDC(hwnd);
        r->custom_mine_dc = CreateCompatibleDC(screen);
        SelectObject(r->custom_mine_dc, r->custom_mine_bmp);
        ReleaseDC(hwnd, screen);
        r->mine_icon = 5;
    }
}
