#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

/* ---- Cell flags (packed into uint8_t) ---- */
#define CELL_MINE      0x01
#define CELL_REVEALED  0x02
#define CELL_FLAGGED   0x04
#define CELL_QUESTION  0x08

/* ---- Difficulty presets ---- */
typedef enum {
    DIFF_BEGINNER,      /* 9x9,   10 mines */
    DIFF_INTERMEDIATE,  /* 16x16, 40 mines */
    DIFF_EXPERT,        /* 30x16, 99 mines */
    DIFF_CUSTOM
} Difficulty;

/* ---- Cell ---- */
typedef struct {
    uint8_t flags;      /* CELL_MINE | CELL_REVEALED | CELL_FLAGGED | CELL_QUESTION */
    uint8_t number;     /* adjacent mine count 0-8 */
} Cell;

/* ---- Board ---- */
typedef enum {
    STATE_READY,
    STATE_PLAYING,
    STATE_WON,
    STATE_LOST
} GameState;

typedef struct {
    int width, height, mine_count;
    Cell *cells;            /* flat array [height * width] */
    int revealed_count;
    int flagged_count;
    GameState state;
    int first_click;        /* 1 if first click not yet done */
    int toroidal;           /* 1 = edges wrap around (donut topology) */
} Board;

/* ---- API ---- */
Board *board_create(int w, int h, int mines);
void   board_destroy(Board *b);
void   board_reset(Board *b);

Cell  *board_cell(Board *b, int x, int y);
int    board_in_bounds(Board *b, int x, int y);

/* Place mines randomly, keeping (sx,sy) and its 8 neighbors clear */
void   board_place_mines(Board *b, int sx, int sy);

/* Compute number for each cell */
void   board_compute_numbers(Board *b);

/* Reveal cell at (x,y). Returns 1 if mine hit. Uses BFS flood fill for 0-cells. */
int    board_reveal(Board *b, int x, int y);

/* Toggle flag at (x,y) */
void   board_toggle_flag(Board *b, int x, int y);

/* Chord: if flagged neighbors == number, reveal all unflagged neighbors.
   Returns 1 if mine hit. */
int    board_chord(Board *b, int x, int y);

/* Check if won (all non-mine cells revealed) */
int    board_check_win(Board *b);

/* Reveal all mines (called on loss) */
void   board_reveal_all_mines(Board *b);

/* Count adjacent mines */
int    board_count_adjacent_mines(Board *b, int x, int y);

/* Count adjacent flags */
int    board_count_adjacent_flags(Board *b, int x, int y);

/* Count adjacent unrevealed */
int    board_count_adjacent_unrevealed(Board *b, int x, int y);

/* Direction offsets for 8 neighbors */
extern const int DX[8];
extern const int DY[8];

/* Get neighbor coordinates, respecting toroidal mode.
   Returns 1 if valid neighbor, fills *nx, *ny. */
int board_get_neighbor(Board *b, int x, int y, int dir, int *nx, int *ny);

/* Collect cells that WOULD be revealed by clicking (x,y), in BFS order.
   Does NOT modify the board. out_cells must be pre-allocated [w*h].
   Returns -1 if mine hit (reveals the mine cell), otherwise count. */
int board_reveal_collect(Board *b, int x, int y, int *out_cells, int *out_count);

/* Reveal a single cell by flat index (no BFS, no win check). */
void board_reveal_single(Board *b, int flat_idx);

#endif /* BOARD_H */
