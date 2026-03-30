#include "board.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Direction offsets for 8 neighbors */
const int DX[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };
const int DY[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static int idx(Board *b, int x, int y)
{
    return y * b->width + x;
}

/* ------------------------------------------------------------------ */
/*  Neighbor lookup (toroidal-aware)                                   */
/* ------------------------------------------------------------------ */

int board_get_neighbor(Board *b, int x, int y, int dir, int *nx, int *ny)
{
    int tx = x + DX[dir];
    int ty = y + DY[dir];
    if (b->toroidal) {
        *nx = (tx + b->width) % b->width;
        *ny = (ty + b->height) % b->height;
        return 1;  /* always valid in toroidal mode */
    } else {
        *nx = tx;
        *ny = ty;
        return board_in_bounds(b, tx, ty);
    }
}

/* ------------------------------------------------------------------ */
/*  Creation / destruction / reset                                     */
/* ------------------------------------------------------------------ */

Board *board_create(int w, int h, int mines)
{
    Board *b = (Board *)malloc(sizeof(Board));
    if (!b) return NULL;

    b->width      = w;
    b->height     = h;
    b->mine_count = mines;
    b->cells      = (Cell *)calloc((size_t)w * h, sizeof(Cell));
    if (!b->cells) { free(b); return NULL; }

    b->revealed_count = 0;
    b->flagged_count  = 0;
    b->state          = STATE_READY;
    b->first_click    = 1;
    b->toroidal       = 0;  /* default: normal mode */

    return b;
}

void board_destroy(Board *b)
{
    if (!b) return;
    free(b->cells);
    free(b);
}

void board_reset(Board *b)
{
    if (!b) return;
    int total = b->width * b->height;
    memset(b->cells, 0, (size_t)total * sizeof(Cell));
    b->revealed_count = 0;
    b->flagged_count  = 0;
    b->state          = STATE_READY;
    b->first_click    = 1;
    /* Don't reset toroidal — it's a game mode setting, not per-game state */
}

/* ------------------------------------------------------------------ */
/*  Accessors                                                          */
/* ------------------------------------------------------------------ */

Cell *board_cell(Board *b, int x, int y)
{
    if (!board_in_bounds(b, x, y)) return NULL;
    return &b->cells[idx(b, x, y)];
}

int board_in_bounds(Board *b, int x, int y)
{
    return x >= 0 && x < b->width && y >= 0 && y < b->height;
}

/* ------------------------------------------------------------------ */
/*  Mine placement                                                     */
/* ------------------------------------------------------------------ */

void board_place_mines(Board *b, int sx, int sy)
{
    static int seeded = 0;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = 1;
    }

    int total = b->width * b->height;
    int placed = 0;

    while (placed < b->mine_count) {
        int r = rand() % total;
        int rx = r % b->width;
        int ry = r / b->width;

        /* Skip if already a mine */
        if (b->cells[r].flags & CELL_MINE) continue;

        /* Skip the safe zone: (sx,sy) itself */
        if (rx == sx && ry == sy) continue;

        /* Skip 8 neighbors of (sx,sy), respecting toroidal mode */
        int in_safe_zone = 0;
        for (int d = 0; d < 8; d++) {
            int nx, ny;
            if (!board_get_neighbor(b, sx, sy, d, &nx, &ny)) continue;
            if (rx == nx && ry == ny) {
                in_safe_zone = 1;
                break;
            }
        }
        if (in_safe_zone) continue;

        b->cells[r].flags |= CELL_MINE;
        placed++;
    }
}

/* ------------------------------------------------------------------ */
/*  Number computation                                                 */
/* ------------------------------------------------------------------ */

int board_count_adjacent_mines(Board *b, int x, int y)
{
    int count = 0;
    for (int d = 0; d < 8; d++) {
        int nx, ny;
        if (!board_get_neighbor(b, x, y, d, &nx, &ny)) continue;
        if (b->cells[idx(b, nx, ny)].flags & CELL_MINE)
            count++;
    }
    return count;
}

int board_count_adjacent_flags(Board *b, int x, int y)
{
    int count = 0;
    for (int d = 0; d < 8; d++) {
        int nx, ny;
        if (!board_get_neighbor(b, x, y, d, &nx, &ny)) continue;
        if (b->cells[idx(b, nx, ny)].flags & CELL_FLAGGED)
            count++;
    }
    return count;
}

int board_count_adjacent_unrevealed(Board *b, int x, int y)
{
    int count = 0;
    for (int d = 0; d < 8; d++) {
        int nx, ny;
        if (!board_get_neighbor(b, x, y, d, &nx, &ny)) continue;
        if (!(b->cells[idx(b, nx, ny)].flags & CELL_REVEALED))
            count++;
    }
    return count;
}

void board_compute_numbers(Board *b)
{
    for (int y = 0; y < b->height; y++) {
        for (int x = 0; x < b->width; x++) {
            Cell *c = &b->cells[idx(b, x, y)];
            if (c->flags & CELL_MINE) {
                c->number = 0;  /* mines don't need a number */
            } else {
                c->number = (uint8_t)board_count_adjacent_mines(b, x, y);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Reveal (BFS flood fill)                                            */
/* ------------------------------------------------------------------ */

int board_reveal(Board *b, int x, int y)
{
    if (!board_in_bounds(b, x, y)) return 0;

    Cell *c = &b->cells[idx(b, x, y)];

    /* Cannot reveal flagged or already revealed cells */
    if (c->flags & (CELL_REVEALED | CELL_FLAGGED)) return 0;

    /* First click: place mines and compute numbers */
    if (b->first_click) {
        b->first_click = 0;
        b->state = STATE_PLAYING;
        board_place_mines(b, x, y);
        board_compute_numbers(b);
    }

    /* Hit a mine */
    if (c->flags & CELL_MINE) {
        c->flags |= CELL_REVEALED;
        b->state = STATE_LOST;
        board_reveal_all_mines(b);
        return 1;
    }

    /* BFS flood fill */
    int total = b->width * b->height;
    int *queue = (int *)malloc((size_t)total * sizeof(int));
    if (!queue) return 0;

    int head = 0, tail = 0;

    /* Reveal starting cell */
    c->flags |= CELL_REVEALED;
    b->revealed_count++;
    queue[tail++] = idx(b, x, y);

    while (head < tail) {
        int cur = queue[head++];
        int cx = cur % b->width;
        int cy = cur / b->width;
        Cell *cc = &b->cells[cur];

        /* Only flood through zero-numbered cells */
        if (cc->number != 0) continue;

        for (int d = 0; d < 8; d++) {
            int nx, ny;
            if (!board_get_neighbor(b, cx, cy, d, &nx, &ny)) continue;

            int ni = idx(b, nx, ny);
            Cell *nc = &b->cells[ni];

            if (nc->flags & (CELL_REVEALED | CELL_FLAGGED | CELL_MINE)) continue;

            nc->flags |= CELL_REVEALED;
            b->revealed_count++;
            queue[tail++] = ni;
        }
    }

    free(queue);
    board_check_win(b);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Reveal collect (non-destructive BFS for animation)                 */
/* ------------------------------------------------------------------ */

int board_reveal_collect(Board *b, int x, int y, int *out_cells, int *out_count)
{
    *out_count = 0;
    if (!board_in_bounds(b, x, y)) return 0;

    Cell *c = board_cell(b, x, y);
    if (c->flags & (CELL_REVEALED | CELL_FLAGGED)) return 0;

    /* Mine hit */
    if (c->flags & CELL_MINE) {
        c->flags |= CELL_REVEALED;
        b->revealed_count++;
        return -1;
    }

    int total = b->width * b->height;
    uint8_t *visited = (uint8_t *)calloc(total, 1);
    if (!visited) return 0;

    /* BFS queue */
    int *queue = (int *)malloc(total * sizeof(int));
    if (!queue) { free(visited); return 0; }
    int qfront = 0, qback = 0;

    int start = y * b->width + x;
    queue[qback++] = start;
    visited[start] = 1;

    while (qfront < qback) {
        int cur = queue[qfront++];
        out_cells[(*out_count)++] = cur;

        int cx = cur % b->width;
        int cy = cur / b->width;
        Cell *cc = &b->cells[cur];

        /* Only expand if this cell is a zero */
        if (cc->number == 0) {
            for (int d = 0; d < 8; d++) {
                int nx, ny;
                if (!board_get_neighbor(b, cx, cy, d, &nx, &ny)) continue;
                int nidx = ny * b->width + nx;
                if (visited[nidx]) continue;
                Cell *nc = &b->cells[nidx];
                if (nc->flags & (CELL_REVEALED | CELL_FLAGGED | CELL_MINE)) continue;
                visited[nidx] = 1;
                queue[qback++] = nidx;
            }
        }
    }

    free(queue);
    free(visited);
    return *out_count;
}

/* ------------------------------------------------------------------ */
/*  Reveal single cell (for animation playback)                        */
/* ------------------------------------------------------------------ */

void board_reveal_single(Board *b, int flat_idx)
{
    if (flat_idx < 0 || flat_idx >= b->width * b->height) return;
    Cell *c = &b->cells[flat_idx];
    if (c->flags & CELL_REVEALED) return;
    c->flags |= CELL_REVEALED;
    c->flags &= ~(CELL_FLAGGED | CELL_QUESTION);
    b->revealed_count++;
}

/* ------------------------------------------------------------------ */
/*  Flag toggling                                                      */
/* ------------------------------------------------------------------ */

void board_toggle_flag(Board *b, int x, int y)
{
    if (!board_in_bounds(b, x, y)) return;

    Cell *c = &b->cells[idx(b, x, y)];

    /* Cannot flag revealed cells */
    if (c->flags & CELL_REVEALED) return;

    if (c->flags & CELL_FLAGGED) {
        /* Flagged -> Question */
        c->flags &= ~CELL_FLAGGED;
        c->flags |= CELL_QUESTION;
        b->flagged_count--;
    } else if (c->flags & CELL_QUESTION) {
        /* Question -> Normal */
        c->flags &= ~CELL_QUESTION;
    } else {
        /* Normal -> Flagged */
        c->flags |= CELL_FLAGGED;
        b->flagged_count++;
    }
}

/* ------------------------------------------------------------------ */
/*  Chord                                                              */
/* ------------------------------------------------------------------ */

int board_chord(Board *b, int x, int y)
{
    if (!board_in_bounds(b, x, y)) return 0;

    Cell *c = &b->cells[idx(b, x, y)];

    /* Can only chord on revealed numbered cells */
    if (!(c->flags & CELL_REVEALED)) return 0;
    if (c->number == 0) return 0;

    /* Check if flagged neighbor count matches the number */
    int adj_flags = board_count_adjacent_flags(b, x, y);
    if (adj_flags != c->number) return 0;

    int hit_mine = 0;

    for (int d = 0; d < 8; d++) {
        int nx, ny;
        if (!board_get_neighbor(b, x, y, d, &nx, &ny)) continue;

        Cell *nc = &b->cells[idx(b, nx, ny)];

        /* Reveal only unflagged unrevealed neighbors */
        if (nc->flags & (CELL_REVEALED | CELL_FLAGGED)) continue;

        if (board_reveal(b, nx, ny))
            hit_mine = 1;
    }

    return hit_mine;
}

/* ------------------------------------------------------------------ */
/*  Win check                                                          */
/* ------------------------------------------------------------------ */

int board_check_win(Board *b)
{
    if (b->state == STATE_LOST) return 0;

    int total = b->width * b->height;
    int non_mine = total - b->mine_count;

    if (b->revealed_count >= non_mine) {
        b->state = STATE_WON;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Reveal all mines (on loss)                                         */
/* ------------------------------------------------------------------ */

void board_reveal_all_mines(Board *b)
{
    int total = b->width * b->height;
    for (int i = 0; i < total; i++) {
        if (b->cells[i].flags & CELL_MINE)
            b->cells[i].flags |= CELL_REVEALED;
    }
}
