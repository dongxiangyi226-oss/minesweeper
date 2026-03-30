#include "solver.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/* Deep-copy a board (cells array is duplicated). */
static Board *board_copy(const Board *src)
{
    Board *dst = (Board *)malloc(sizeof(Board));
    if (!dst) return NULL;
    *dst = *src;
    int n = src->width * src->height;
    dst->cells = (Cell *)malloc(n * sizeof(Cell));
    if (!dst->cells) { free(dst); return NULL; }
    memcpy(dst->cells, src->cells, n * sizeof(Cell));
    return dst;
}

static void board_copy_free(Board *b)
{
    if (b) { free(b->cells); free(b); }
}

/* Get the flat index for (x, y). */
static int idx(const Board *b, int x, int y)
{
    return y * b->width + x;
}

/* Collect unrevealed, unflagged neighbors of (x,y) into buf[].
   Returns the count. */
static int get_unrevealed_neighbors(Board *b, int x, int y,
                                    int *buf_x, int *buf_y)
{
    int count = 0;
    for (int d = 0; d < 8; d++) {
        int nx = x + DX[d];
        int ny = y + DY[d];
        if (!board_in_bounds(b, nx, ny)) continue;
        Cell *c = board_cell(b, nx, ny);
        if (!(c->flags & CELL_REVEALED) && !(c->flags & CELL_FLAGGED)) {
            buf_x[count] = nx;
            buf_y[count] = ny;
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/*  Constraint-based solver (Rules 1, 2, 3)                           */
/* ------------------------------------------------------------------ */

/* Apply Rule 1 (trivial safe) and Rule 2 (trivial mine) once over the
   whole board.  Returns number of actions taken. */
static int apply_trivial_rules(Board *b)
{
    int actions = 0;
    int w = b->width, h = b->height;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Cell *c = board_cell(b, x, y);
            if (!(c->flags & CELL_REVEALED)) continue;
            if (c->number == 0) continue;

            int adj_flags = board_count_adjacent_flags(b, x, y);
            int adj_unrev = board_count_adjacent_unrevealed(b, x, y);
            if (adj_unrev == 0) continue;

            int remaining = c->number - adj_flags;

            /* Rule 1: all mines accounted for -> remaining neighbors safe */
            if (remaining == 0) {
                int bx[8], by[8];
                int cnt = get_unrevealed_neighbors(b, x, y, bx, by);
                for (int i = 0; i < cnt; i++) {
                    board_reveal(b, bx[i], by[i]);
                    actions++;
                }
            }
            /* Rule 2: remaining == unrevealed -> all are mines */
            else if (remaining == adj_unrev) {
                int bx[8], by[8];
                int cnt = get_unrevealed_neighbors(b, x, y, bx, by);
                for (int i = 0; i < cnt; i++) {
                    Cell *nc = board_cell(b, bx[i], by[i]);
                    if (!(nc->flags & CELL_FLAGGED)) {
                        board_toggle_flag(b, bx[i], by[i]);
                        actions++;
                    }
                }
            }
        }
    }
    return actions;
}

/* Apply Rule 3 (constraint subtraction) once.
   For every pair of adjacent revealed numbered cells A, B:
     Let SA = unrevealed unflagged neighbors of A, rA = number_A - flags_A
     Let SB = unrevealed unflagged neighbors of B, rB = number_B - flags_B
     If SA ⊂ SB:
       diff_count = |SB \ SA|, diff_mines = rB - rA
       If diff_mines == 0: cells in SB\SA are safe -> reveal
       If diff_mines == diff_count: cells in SB\SA are mines -> flag
     (Also check SB ⊂ SA symmetrically.)
   Returns number of actions taken. */
static int apply_constraint_subtraction(Board *b)
{
    int actions = 0;
    int w = b->width, h = b->height;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Cell *cA = board_cell(b, x, y);
            if (!(cA->flags & CELL_REVEALED) || cA->number == 0) continue;

            int sA_x[8], sA_y[8];
            int nA = get_unrevealed_neighbors(b, x, y, sA_x, sA_y);
            if (nA == 0) continue;
            int rA = cA->number - board_count_adjacent_flags(b, x, y);

            /* Look at each neighbor of (x,y) that is also revealed+numbered */
            for (int d = 0; d < 8; d++) {
                int bx = x + DX[d];
                int by = y + DY[d];
                if (!board_in_bounds(b, bx, by)) continue;
                Cell *cB = board_cell(b, bx, by);
                if (!(cB->flags & CELL_REVEALED) || cB->number == 0) continue;

                int sB_x[8], sB_y[8];
                int nB = get_unrevealed_neighbors(b, bx, by, sB_x, sB_y);
                if (nB == 0) continue;
                int rB = cB->number - board_count_adjacent_flags(b, bx, by);

                /* Check if SA ⊂ SB */
                /* Build bitmask of SA positions in a local set */
                int sa_subset_of_sb = 1;
                for (int i = 0; i < nA && sa_subset_of_sb; i++) {
                    int found = 0;
                    for (int j = 0; j < nB; j++) {
                        if (sA_x[i] == sB_x[j] && sA_y[i] == sB_y[j]) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) sa_subset_of_sb = 0;
                }

                if (sa_subset_of_sb && nA < nB) {
                    /* SA ⊂ SB (proper subset) */
                    /* Compute SB \ SA */
                    int diff_x[8], diff_y[8];
                    int diff_count = 0;
                    for (int j = 0; j < nB; j++) {
                        int in_sa = 0;
                        for (int i = 0; i < nA; i++) {
                            if (sB_x[j] == sA_x[i] && sB_y[j] == sA_y[i]) {
                                in_sa = 1;
                                break;
                            }
                        }
                        if (!in_sa) {
                            diff_x[diff_count] = sB_x[j];
                            diff_y[diff_count] = sB_y[j];
                            diff_count++;
                        }
                    }
                    int diff_mines = rB - rA;
                    if (diff_mines == 0) {
                        /* Cells in SB\SA are safe */
                        for (int k = 0; k < diff_count; k++) {
                            Cell *dc = board_cell(b, diff_x[k], diff_y[k]);
                            if (!(dc->flags & CELL_REVEALED) &&
                                !(dc->flags & CELL_FLAGGED)) {
                                board_reveal(b, diff_x[k], diff_y[k]);
                                actions++;
                            }
                        }
                    } else if (diff_mines == diff_count && diff_mines > 0) {
                        /* Cells in SB\SA are all mines */
                        for (int k = 0; k < diff_count; k++) {
                            Cell *dc = board_cell(b, diff_x[k], diff_y[k]);
                            if (!(dc->flags & CELL_FLAGGED)) {
                                board_toggle_flag(b, diff_x[k], diff_y[k]);
                                actions++;
                            }
                        }
                    }
                }
            }
        }
    }
    return actions;
}

/* ------------------------------------------------------------------ */
/*  solver_solve                                                       */
/* ------------------------------------------------------------------ */

SolverResult solver_solve(Board *b)
{
    SolverResult res;
    Board *copy = board_copy(b);
    if (!copy) {
        res.cells_revealed = 0;
        res.cells_flagged  = 0;
        res.complete = 0;
        return res;
    }

    int initial_revealed = copy->revealed_count;
    int initial_flagged  = copy->flagged_count;

    /* Iteratively apply all rules until no progress */
    for (;;) {
        int progress = 0;
        progress += apply_trivial_rules(copy);
        if (copy->state == STATE_LOST) break;
        progress += apply_constraint_subtraction(copy);
        if (copy->state == STATE_LOST) break;
        if (progress == 0) break;
    }

    res.cells_revealed = copy->revealed_count - initial_revealed;
    res.cells_flagged  = copy->flagged_count  - initial_flagged;
    res.complete = (copy->revealed_count ==
                    copy->width * copy->height - copy->mine_count) ? 1 : 0;

    board_copy_free(copy);
    return res;
}

/* ------------------------------------------------------------------ */
/*  solver_generate_no_guess                                           */
/* ------------------------------------------------------------------ */

int solver_generate_no_guess(Board *b, int sx, int sy, int max_attempts)
{
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        board_reset(b);
        board_place_mines(b, sx, sy);
        board_compute_numbers(b);

        /* Work on a copy: reveal the start cell, then solve */
        Board *copy = board_copy(b);
        if (!copy) return 0;

        copy->first_click = 0;
        copy->state = STATE_PLAYING;
        board_reveal(copy, sx, sy);

        /* Run solver iterations on the copy directly */
        for (;;) {
            int progress = 0;
            progress += apply_trivial_rules(copy);
            if (copy->state == STATE_LOST) break;
            progress += apply_constraint_subtraction(copy);
            if (copy->state == STATE_LOST) break;
            if (progress == 0) break;
        }

        int total_cells = copy->width * copy->height;
        int complete = (copy->revealed_count == total_cells - copy->mine_count);
        board_copy_free(copy);

        if (complete) {
            /* This board layout works — keep it.
               The original board b already has the mines + numbers set,
               and is in its unrevealed state (board_reset then place+compute). */
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  solver_get_hint                                                    */
/* ------------------------------------------------------------------ */

int solver_get_hint(Board *b, int *hx, int *hy)
{
    Board *copy = board_copy(b);
    if (!copy) return 0;

    int w = copy->width, h = copy->height;

    /* One pass of Rules 1 & 2.  We look for the first safe reveal. */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Cell *c = board_cell(copy, x, y);
            if (!(c->flags & CELL_REVEALED) || c->number == 0) continue;

            int adj_flags = board_count_adjacent_flags(copy, x, y);
            int adj_unrev = board_count_adjacent_unrevealed(copy, x, y);
            if (adj_unrev == 0) continue;

            int remaining = c->number - adj_flags;

            if (remaining == 0) {
                /* First unrevealed unflagged neighbor is safe */
                for (int d = 0; d < 8; d++) {
                    int nx = x + DX[d];
                    int ny = y + DY[d];
                    if (!board_in_bounds(copy, nx, ny)) continue;
                    Cell *nc = board_cell(copy, nx, ny);
                    if (!(nc->flags & CELL_REVEALED) &&
                        !(nc->flags & CELL_FLAGGED)) {
                        *hx = nx;
                        *hy = ny;
                        board_copy_free(copy);
                        return 1;
                    }
                }
            }
        }
    }

    /* If trivial rules gave nothing, try constraint subtraction for safe cells */
    /* First, apply Rule 2 (flagging) so constraint subtraction has updated info */
    apply_trivial_rules(copy);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Cell *cA = board_cell(copy, x, y);
            if (!(cA->flags & CELL_REVEALED) || cA->number == 0) continue;

            int sA_x[8], sA_y[8];
            int nA = get_unrevealed_neighbors(copy, x, y, sA_x, sA_y);
            if (nA == 0) continue;
            int rA = cA->number - board_count_adjacent_flags(copy, x, y);

            for (int d = 0; d < 8; d++) {
                int bx = x + DX[d];
                int by = y + DY[d];
                if (!board_in_bounds(copy, bx, by)) continue;
                Cell *cB = board_cell(copy, bx, by);
                if (!(cB->flags & CELL_REVEALED) || cB->number == 0) continue;

                int sB_x[8], sB_y[8];
                int nB = get_unrevealed_neighbors(copy, bx, by, sB_x, sB_y);
                if (nB == 0) continue;
                int rB = cB->number - board_count_adjacent_flags(copy, bx, by);

                /* Check SA ⊂ SB */
                int sa_subset_of_sb = 1;
                for (int i = 0; i < nA && sa_subset_of_sb; i++) {
                    int found = 0;
                    for (int j = 0; j < nB; j++) {
                        if (sA_x[i] == sB_x[j] && sA_y[i] == sB_y[j]) {
                            found = 1; break;
                        }
                    }
                    if (!found) sa_subset_of_sb = 0;
                }

                if (sa_subset_of_sb && nA < nB) {
                    int diff_mines = rB - rA;
                    if (diff_mines == 0) {
                        /* Cells in SB\SA are safe */
                        for (int j = 0; j < nB; j++) {
                            int in_sa = 0;
                            for (int i = 0; i < nA; i++) {
                                if (sB_x[j] == sA_x[i] && sB_y[j] == sA_y[i]) {
                                    in_sa = 1; break;
                                }
                            }
                            if (!in_sa) {
                                *hx = sB_x[j];
                                *hy = sB_y[j];
                                board_copy_free(copy);
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }

    board_copy_free(copy);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Probability computation: frontier enumeration / sampling           */
/* ------------------------------------------------------------------ */

/* Max frontier component size for exhaustive enumeration */
#define MAX_ENUM_SIZE 20
#define SAMPLE_COUNT  100

/* ---- Connected-component labeling of frontier cells ---- */

/* A frontier cell is unrevealed, unflagged, and adjacent to at least one
   revealed numbered cell. */
static int is_frontier(Board *b, int x, int y)
{
    Cell *c = board_cell(b, x, y);
    if (c->flags & (CELL_REVEALED | CELL_FLAGGED)) return 0;
    for (int d = 0; d < 8; d++) {
        int nx = x + DX[d];
        int ny = y + DY[d];
        if (!board_in_bounds(b, nx, ny)) continue;
        Cell *nc = board_cell(b, nx, ny);
        if ((nc->flags & CELL_REVEALED) && nc->number > 0) return 1;
    }
    return 0;
}

/* Union-Find for component grouping */
static int uf_find(int *parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

static void uf_union(int *parent, int *rank, int a, int b_val)
{
    int ra = uf_find(parent, a);
    int rb = uf_find(parent, b_val);
    if (ra == rb) return;
    if (rank[ra] < rank[rb]) { int t = ra; ra = rb; rb = t; }
    parent[rb] = ra;
    if (rank[ra] == rank[rb]) rank[ra]++;
}

/* ---- Constraint checking for backtracking ---- */

/* For a set of frontier cells with a (partial) mine assignment, check that
   every revealed neighbor's constraint is consistent.
   assignment[i]: 0 = safe, 1 = mine, -1 = unassigned
   Returns 1 if consistent so far. */
static int check_constraints(Board *b, int *fx, int *fy, int *assignment,
                             int n, int assigned_count)
{
    /* Check each revealed numbered cell adjacent to any assigned frontier cell */
    /* We iterate assigned frontier cells, check their revealed neighbors */
    for (int i = 0; i < assigned_count; i++) {
        int cx = fx[i], cy = fy[i];
        for (int d = 0; d < 8; d++) {
            int rx = cx + DX[d];
            int ry = cy + DY[d];
            if (!board_in_bounds(b, rx, ry)) continue;
            Cell *rc = board_cell(b, rx, ry);
            if (!(rc->flags & CELL_REVEALED) || rc->number == 0) continue;

            int adj_flags = board_count_adjacent_flags(b, rx, ry);
            int remaining = rc->number - adj_flags;

            /* Count mines and unknowns among this revealed cell's
               unrevealed unflagged neighbors (which may be frontier cells) */
            int mine_count = 0;
            int unknown_count = 0;

            for (int dd = 0; dd < 8; dd++) {
                int ux = rx + DX[dd];
                int uy = ry + DY[dd];
                if (!board_in_bounds(b, ux, uy)) continue;
                Cell *uc = board_cell(b, ux, uy);
                if (uc->flags & (CELL_REVEALED | CELL_FLAGGED)) continue;

                /* Check if this neighbor is in our frontier set */
                int fidx = -1;
                for (int k = 0; k < n; k++) {
                    if (fx[k] == ux && fy[k] == uy) {
                        fidx = k; break;
                    }
                }
                if (fidx >= 0 && fidx < assigned_count) {
                    if (assignment[fidx] == 1) mine_count++;
                } else {
                    unknown_count++;
                }
            }

            /* Too many mines already */
            if (mine_count > remaining) return 0;
            /* Not enough unknowns to fill remaining mines */
            if (mine_count + unknown_count < remaining) return 0;
        }
    }
    return 1;
}

/* Check if assignment is fully consistent (all constraints exactly met
   for revealed cells whose ENTIRE unrevealed neighborhood is in the
   component). For cells with external unknowns, the partial check above
   suffices. */
static int check_full_constraints(Board *b, int *fx, int *fy,
                                  int *assignment, int n)
{
    /* For each revealed numbered cell adjacent to any frontier cell in this
       component, if ALL its unrevealed unflagged neighbors are in the
       component (assigned_count == n), then mine_count must == remaining. */
    for (int i = 0; i < n; i++) {
        int cx = fx[i], cy = fy[i];
        for (int d = 0; d < 8; d++) {
            int rx = cx + DX[d];
            int ry = cy + DY[d];
            if (!board_in_bounds(b, rx, ry)) continue;
            Cell *rc = board_cell(b, rx, ry);
            if (!(rc->flags & CELL_REVEALED) || rc->number == 0) continue;

            int adj_flags = board_count_adjacent_flags(b, rx, ry);
            int remaining = rc->number - adj_flags;

            int mine_count = 0;
            int external_unknown = 0;

            for (int dd = 0; dd < 8; dd++) {
                int ux = rx + DX[dd];
                int uy = ry + DY[dd];
                if (!board_in_bounds(b, ux, uy)) continue;
                Cell *uc = board_cell(b, ux, uy);
                if (uc->flags & (CELL_REVEALED | CELL_FLAGGED)) continue;

                int fidx = -1;
                for (int k = 0; k < n; k++) {
                    if (fx[k] == ux && fy[k] == uy) {
                        fidx = k; break;
                    }
                }
                if (fidx >= 0) {
                    if (assignment[fidx] == 1) mine_count++;
                } else {
                    external_unknown++;
                }
            }

            if (external_unknown == 0 && mine_count != remaining) return 0;
            if (mine_count > remaining) return 0;
            if (mine_count + external_unknown < remaining) return 0;
        }
    }
    return 1;
}

/* ---- Exhaustive backtracking for small components ---- */

/* Enumerate valid assignments for frontier component.
   mine_count_out[i] = number of valid assignments where cell i is a mine.
   total_count_out = total number of valid assignments. */
static void enumerate_component(Board *b, int *fx, int *fy, int n,
                                double *mine_count_out, double *total_count_out)
{
    /* assignment[i]: 0 = safe, 1 = mine, 2 = not yet tried at this level */
    int *assignment = (int *)malloc(n * sizeof(int));
    if (!assignment) { *total_count_out = 0; return; }

    for (int i = 0; i < n; i++) mine_count_out[i] = 0.0;
    *total_count_out = 0.0;

    int pos = 0;
    assignment[0] = 0;  /* try safe first */

    while (pos >= 0) {
        /* Pruning: check partial consistency up to pos (inclusive) */
        int ok = check_constraints(b, fx, fy, assignment, n, pos + 1);

        if (ok && pos == n - 1) {
            /* Complete assignment that passes partial check —
               verify full constraints (exact match for fully-enclosed clues) */
            if (check_full_constraints(b, fx, fy, assignment, n)) {
                *total_count_out += 1.0;
                for (int i = 0; i < n; i++) {
                    if (assignment[i] == 1) mine_count_out[i] += 1.0;
                }
            }
            ok = 0;  /* force backtrack to explore next branch */
        }

        if (ok) {
            /* Go deeper */
            pos++;
            assignment[pos] = 0;
        } else {
            /* Backtrack: advance current cell to next value, or go up */
            while (pos >= 0 && assignment[pos] == 1) {
                pos--;  /* already tried both 0 and 1 at this level */
            }
            if (pos >= 0) {
                assignment[pos] = 1;  /* was 0, now try 1 */
            }
        }
    }

    free(assignment);
}

/* ---- Random sampling for large components ---- */

/* Simple xorshift RNG */
static unsigned int rng_state = 12345;

static unsigned int rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void sample_component(Board *b, int *fx, int *fy, int n,
                             double *mine_count_out, double *total_count_out)
{
    int *assignment = (int *)calloc(n, sizeof(int));
    int *order = (int *)malloc(n * sizeof(int));
    if (!assignment || !order) {
        free(assignment); free(order);
        *total_count_out = 0;
        return;
    }

    for (int i = 0; i < n; i++) mine_count_out[i] = 0.0;
    *total_count_out = 0.0;

    for (int s = 0; s < SAMPLE_COUNT; s++) {
        /* Random permutation for assignment order */
        for (int i = 0; i < n; i++) order[i] = i;
        for (int i = n - 1; i > 0; i--) {
            int j = rng_next() % (i + 1);
            int t = order[i]; order[i] = order[j]; order[j] = t;
        }

        /* Greedily assign: for each cell in random order, try random value,
           then the other if it fails constraints. */
        int valid = 1;
        memset(assignment, 0, n * sizeof(int));
        /* We need to check constraints incrementally, but cells are assigned
           in random order. Simpler: assign all, then validate. */

        /* Try: randomly assign each cell to 0 or 1 */
        for (int i = 0; i < n; i++) {
            assignment[i] = (rng_next() & 1);
        }

        /* Validate fully */
        if (!check_full_constraints(b, fx, fy, assignment, n)) {
            /* Try a simple repair: flip cells that violate constraints */
            /* (This is approximate — just retry with different random) */
            valid = 0;
            /* Attempt a few random retries for this sample */
            for (int retry = 0; retry < 20 && !valid; retry++) {
                for (int i = 0; i < n; i++) {
                    assignment[i] = (rng_next() & 1);
                }
                if (check_full_constraints(b, fx, fy, assignment, n)) {
                    valid = 1;
                }
            }
        }

        if (valid) {
            *total_count_out += 1.0;
            for (int i = 0; i < n; i++) {
                if (assignment[i] == 1) mine_count_out[i] += 1.0;
            }
        }
    }

    free(assignment);
    free(order);
}

/* ------------------------------------------------------------------ */
/*  solver_compute_probabilities                                       */
/* ------------------------------------------------------------------ */

void solver_compute_probabilities(Board *b, double *prob_map)
{
    int w = b->width, h = b->height;
    int total = w * h;

    /* Initialize: revealed = -1, flagged = 1, others = -2 (unknown) */
    for (int i = 0; i < total; i++) {
        Cell *c = &b->cells[i];
        if (c->flags & CELL_REVEALED) {
            prob_map[i] = -1.0;
        } else if (c->flags & CELL_FLAGGED) {
            prob_map[i] = 1.0;
        } else {
            prob_map[i] = -2.0;  /* placeholder */
        }
    }

    /* Collect frontier cells */
    int *front_x = (int *)malloc(total * sizeof(int));
    int *front_y = (int *)malloc(total * sizeof(int));
    int *front_id = (int *)malloc(total * sizeof(int));  /* flat index -> frontier index, or -1 */
    if (!front_x || !front_y || !front_id) {
        free(front_x); free(front_y); free(front_id);
        /* Fall back: set all unknowns to 0.5 */
        for (int i = 0; i < total; i++)
            if (prob_map[i] == -2.0) prob_map[i] = 0.5;
        return;
    }
    for (int i = 0; i < total; i++) front_id[i] = -1;

    int nfront = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (is_frontier(b, x, y)) {
                front_id[y * w + x] = nfront;
                front_x[nfront] = x;
                front_y[nfront] = y;
                nfront++;
            }
        }
    }

    if (nfront == 0) {
        /* No frontier — all unknowns are interior */
        int interior_count = 0;
        for (int i = 0; i < total; i++)
            if (prob_map[i] == -2.0) interior_count++;
        int remaining_mines = b->mine_count - b->flagged_count;
        double p = (interior_count > 0)
                   ? (double)remaining_mines / interior_count : 0.0;
        for (int i = 0; i < total; i++)
            if (prob_map[i] == -2.0) prob_map[i] = p;
        free(front_x); free(front_y); free(front_id);
        return;
    }

    /* Group frontier cells into connected components.
       Two frontier cells are connected if they share a revealed numbered
       neighbor (i.e., they constrain each other). */
    int *parent = (int *)malloc(nfront * sizeof(int));
    int *rank_arr = (int *)calloc(nfront, sizeof(int));
    if (!parent || !rank_arr) {
        free(front_x); free(front_y); free(front_id);
        free(parent); free(rank_arr);
        for (int i = 0; i < total; i++)
            if (prob_map[i] == -2.0) prob_map[i] = 0.5;
        return;
    }
    for (int i = 0; i < nfront; i++) parent[i] = i;

    /* For each revealed numbered cell, union all its frontier neighbors */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Cell *c = board_cell(b, x, y);
            if (!(c->flags & CELL_REVEALED) || c->number == 0) continue;

            int first_front = -1;
            for (int d = 0; d < 8; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                if (!board_in_bounds(b, nx, ny)) continue;
                int fi = front_id[ny * w + nx];
                if (fi < 0) continue;
                if (first_front < 0) {
                    first_front = fi;
                } else {
                    uf_union(parent, rank_arr, first_front, fi);
                }
            }
        }
    }

    /* Collect components */
    /* comp_root -> list of frontier indices */
    int *comp_label = (int *)malloc(nfront * sizeof(int));
    int num_comps = 0;
    int *root_map = (int *)malloc(nfront * sizeof(int)); /* root -> comp id */
    if (!comp_label || !root_map) {
        free(front_x); free(front_y); free(front_id);
        free(parent); free(rank_arr); free(comp_label); free(root_map);
        for (int i = 0; i < total; i++)
            if (prob_map[i] == -2.0) prob_map[i] = 0.5;
        return;
    }
    for (int i = 0; i < nfront; i++) root_map[i] = -1;

    for (int i = 0; i < nfront; i++) {
        int r = uf_find(parent, i);
        if (root_map[r] < 0) {
            root_map[r] = num_comps++;
        }
        comp_label[i] = root_map[r];
    }

    /* For each component, gather its cells and compute probabilities */
    /* Allocate per-component arrays */
    int *comp_size = (int *)calloc(num_comps, sizeof(int));
    int **comp_fx = (int **)malloc(num_comps * sizeof(int *));
    int **comp_fy = (int **)malloc(num_comps * sizeof(int *));
    if (!comp_size || !comp_fx || !comp_fy) goto cleanup;

    for (int i = 0; i < nfront; i++) comp_size[comp_label[i]]++;

    for (int c = 0; c < num_comps; c++) {
        comp_fx[c] = (int *)malloc(comp_size[c] * sizeof(int));
        comp_fy[c] = (int *)malloc(comp_size[c] * sizeof(int));
        comp_size[c] = 0;  /* reset to use as insert index */
    }

    for (int i = 0; i < nfront; i++) {
        int c = comp_label[i];
        int pos = comp_size[c]++;
        comp_fx[c][pos] = front_x[i];
        comp_fy[c][pos] = front_y[i];
    }

    /* Process each component */
    for (int c = 0; c < num_comps; c++) {
        int cn = comp_size[c];
        double *mine_counts = (double *)calloc(cn, sizeof(double));
        double total_count = 0.0;
        if (!mine_counts) continue;

        if (cn <= MAX_ENUM_SIZE) {
            enumerate_component(b, comp_fx[c], comp_fy[c], cn,
                                mine_counts, &total_count);
        } else {
            sample_component(b, comp_fx[c], comp_fy[c], cn,
                             mine_counts, &total_count);
        }

        for (int i = 0; i < cn; i++) {
            int fi = idx(b, comp_fx[c][i], comp_fy[c][i]);
            if (total_count > 0) {
                prob_map[fi] = mine_counts[i] / total_count;
            } else {
                /* No valid assignments found — use 0.5 as fallback */
                prob_map[fi] = 0.5;
            }
        }

        free(mine_counts);
    }

    /* Interior cells: unrevealed, unflagged, not frontier */
    {
        int interior_count = 0;
        int remaining_mines = b->mine_count - b->flagged_count;

        /* Estimate mines in frontier from probabilities */
        double expected_frontier_mines = 0.0;
        for (int i = 0; i < nfront; i++) {
            int fi = idx(b, front_x[i], front_y[i]);
            if (prob_map[fi] >= 0.0 && prob_map[fi] <= 1.0) {
                expected_frontier_mines += prob_map[fi];
            }
        }

        for (int i = 0; i < total; i++) {
            if (prob_map[i] == -2.0) interior_count++;
        }

        double interior_mines = (double)remaining_mines - expected_frontier_mines;
        if (interior_mines < 0) interior_mines = 0;
        double p = (interior_count > 0) ? interior_mines / interior_count : 0.0;
        if (p > 1.0) p = 1.0;
        if (p < 0.0) p = 0.0;

        for (int i = 0; i < total; i++) {
            if (prob_map[i] == -2.0) prob_map[i] = p;
        }
    }

    /* Free per-component arrays */
    for (int c = 0; c < num_comps; c++) {
        free(comp_fx[c]);
        free(comp_fy[c]);
    }

cleanup:
    free(comp_fx);
    free(comp_fy);
    free(comp_size);
    free(comp_label);
    free(root_map);
    free(parent);
    free(rank_arr);
    free(front_x);
    free(front_y);
    free(front_id);
}
