#include <stdlib.h>
#include "game.h"
#include "board.h"
#include "solver.h"

/* ------------------------------------------------------------------ */
/*  Dimension presets                                                  */
/* ------------------------------------------------------------------ */
void game_get_dimensions(Difficulty diff, int *w, int *h, int *mines)
{
    switch (diff) {
    case DIFF_BEGINNER:
        *w = 9;  *h = 9;  *mines = 10;
        break;
    case DIFF_INTERMEDIATE:
        *w = 16; *h = 16; *mines = 40;
        break;
    case DIFF_EXPERT:
        *w = 30; *h = 16; *mines = 99;
        break;
    default:                       /* DIFF_CUSTOM — caller fills in */
        *w = 9;  *h = 9;  *mines = 10;
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Create / destroy                                                   */
/* ------------------------------------------------------------------ */
Game *game_create(void)
{
    Game *g = calloc(1, sizeof(Game));
    if (!g) return NULL;

    g->difficulty     = DIFF_BEGINNER;
    g->no_guess_mode  = 1;
    g->fog_mode       = 0;
    g->fog_radius     = 3;
    g->heatmap_on     = 0;
    g->replay_on      = 0;
    g->hint_x         = -1;
    g->hint_y         = -1;
    g->hint_active    = 0;
    g->press_cx       = -1;
    g->press_cy       = -1;
    g->press_active   = 0;
    g->explode_x      = -1;
    g->explode_y      = -1;
    g->board          = NULL;

    /* Create the initial board (beginner) */
    game_new(g, DIFF_BEGINNER);
    return g;
}

void game_destroy(Game *g)
{
    if (!g) return;
    if (g->board) board_destroy(g->board);
    free(g);
}

/* ------------------------------------------------------------------ */
/*  New game                                                           */
/* ------------------------------------------------------------------ */
void game_new(Game *g, Difficulty diff)
{
    int w, h, mines;

    if (diff == DIFF_CUSTOM) {
        w     = g->custom_w;
        h     = g->custom_h;
        mines = g->custom_mines;
    } else {
        game_get_dimensions(diff, &w, &h, &mines);
    }

    g->difficulty      = diff;
    g->elapsed_seconds = 0;
    g->hint_x          = -1;
    g->hint_y          = -1;
    g->hint_active     = 0;
    g->press_cx        = -1;
    g->press_cy        = -1;
    g->press_active    = 0;
    g->explode_x       = -1;
    g->explode_y       = -1;

    if (g->board) board_destroy(g->board);
    g->board = board_create(w, h, mines);
    /* board starts in STATE_READY with first_click = 1 */
}

void game_new_custom(Game *g, int w, int h, int mines)
{
    g->custom_w     = w;
    g->custom_h     = h;
    g->custom_mines = mines;
    game_new(g, DIFF_CUSTOM);
}

/* ------------------------------------------------------------------ */
/*  Left click                                                         */
/* ------------------------------------------------------------------ */
void game_left_click(Game *g, int x, int y)
{
    Board *b = g->board;
    if (!b) return;

    g->hint_active = 0;

    /* Ignore clicks after game over */
    if (b->state == STATE_WON || b->state == STATE_LOST)
        return;

    /* Ignore clicks on flagged cells */
    if (!board_in_bounds(b, x, y)) return;
    Cell *c = board_cell(b, x, y);
    if (c->flags & CELL_FLAGGED) return;

    /* ---- First click: generate the board ---- */
    if (b->state == STATE_READY) {
        if (g->no_guess_mode) {
            solver_generate_no_guess(b, x, y, 1000);
        } else {
            board_place_mines(b, x, y);
            board_compute_numbers(b);
        }
        b->first_click = 0;
        b->state = STATE_PLAYING;

        int hit = board_reveal(b, x, y);
        if (hit) {
            /* Should not happen on first click, but handle defensively */
            b->state = STATE_LOST;
            board_reveal_all_mines(b);
            return;
        }
        if (board_check_win(b)) {
            b->state = STATE_WON;
        }
        return;
    }

    /* ---- Subsequent clicks ---- */
    if (b->state == STATE_PLAYING) {
        int hit = board_reveal(b, x, y);
        if (hit) {
            b->state = STATE_LOST;
            g->explode_x = x;
            g->explode_y = y;
            board_reveal_all_mines(b);
            return;
        }
        if (board_check_win(b)) {
            b->state = STATE_WON;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Right click                                                        */
/* ------------------------------------------------------------------ */
void game_right_click(Game *g, int x, int y)
{
    Board *b = g->board;
    if (!b) return;

    g->hint_active = 0;

    if (b->state == STATE_READY || b->state == STATE_PLAYING) {
        board_toggle_flag(b, x, y);
    }
}

/* ------------------------------------------------------------------ */
/*  Chord click                                                        */
/* ------------------------------------------------------------------ */
void game_chord_click(Game *g, int x, int y)
{
    Board *b = g->board;
    if (!b) return;

    g->hint_active = 0;

    if (b->state != STATE_PLAYING) return;

    int hit = board_chord(b, x, y);
    if (hit) {
        b->state = STATE_LOST;
        board_reveal_all_mines(b);
        return;
    }
    if (board_check_win(b)) {
        b->state = STATE_WON;
    }
}

/* ------------------------------------------------------------------ */
/*  Timer                                                              */
/* ------------------------------------------------------------------ */
void game_tick(Game *g)
{
    if (g->board && g->board->state == STATE_PLAYING) {
        g->elapsed_seconds++;
    }
}

/* ------------------------------------------------------------------ */
/*  Toggles                                                            */
/* ------------------------------------------------------------------ */
void game_toggle_heatmap(Game *g)
{
    g->heatmap_on = !g->heatmap_on;
}

void game_toggle_fog(Game *g)
{
    g->fog_mode = !g->fog_mode;
}

/* ------------------------------------------------------------------ */
/*  Hint                                                               */
/* ------------------------------------------------------------------ */
void game_request_hint(Game *g)
{
    Board *b = g->board;
    if (!b) return;
    if (b->state != STATE_PLAYING) return;

    int hx = -1, hy = -1;
    if (solver_get_hint(b, &hx, &hy)) {
        g->hint_x      = hx;
        g->hint_y      = hy;
        g->hint_active = 1;
    } else {
        g->hint_x      = -1;
        g->hint_y      = -1;
        g->hint_active = 0;
    }
}
