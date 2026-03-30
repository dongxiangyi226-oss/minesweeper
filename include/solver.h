#ifndef SOLVER_H
#define SOLVER_H

#include "board.h"

/* ---- Solver result ---- */
typedef struct {
    int cells_revealed;     /* how many cells the solver revealed */
    int cells_flagged;      /* how many cells the solver flagged  */
    int complete;           /* 1 if all non-mine cells revealed   */
} SolverResult;

/* Run deterministic solver on board (does NOT modify original board).
   Works on a copy. Returns result. */
SolverResult solver_solve(Board *b);

/* ---- No-Guess Generation ---- */
/* Generate a board that is solvable without guessing.
   Places mines, computes numbers, verifies with solver.
   Returns 1 on success, 0 if failed after max_attempts. */
int solver_generate_no_guess(Board *b, int sx, int sy, int max_attempts);

/* ---- Hint System ---- */
/* Find one safe cell to reveal. Returns 1 if found, sets *hx, *hy.
   Returns 0 if no deterministic safe cell found. */
int solver_get_hint(Board *b, int *hx, int *hy);

/* ---- Probability Heat Map ---- */
/* Compute mine probability for each cell.
   prob_map must be pre-allocated [height * width].
   Revealed cells get -1.0, flagged cells get 1.0. */
void solver_compute_probabilities(Board *b, double *prob_map);

/* ---- Step-by-step Solver (for auto-play visualization) ---- */
typedef struct {
    int action;         /* 0 = reveal, 1 = flag */
    int x, y;           /* target cell */
    int reason_x, reason_y;  /* the number cell that triggered deduction */
} SolverAction;

/* Execute one solver step on the board (modifies board directly).
   Returns 1 if action found and applied, 0 if stuck. */
int solver_step(Board *b, SolverAction *out);

#endif /* SOLVER_H */
