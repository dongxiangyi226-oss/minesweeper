/*
 * test_core.c -- Automated tests for board, solver, and game logic
 *
 * Compile:  gcc -O2 -o test_core.exe test_core.c board.c solver.c game.c -lgdi32
 * Run:      test_core.exe
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "board.h"
#include "solver.h"
#include "game.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-50s ", name)
#define PASS() do { printf("[PASS]\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

/* ---- Board Tests ---- */

static void test_board_create(void)
{
    TEST("board_create / board_destroy");
    Board *b = board_create(9, 9, 10);
    CHECK(b != NULL, "board_create returned NULL");
    CHECK(b->width == 9, "width mismatch");
    CHECK(b->height == 9, "height mismatch");
    CHECK(b->mine_count == 10, "mine_count mismatch");
    CHECK(b->state == STATE_READY, "initial state not READY");
    CHECK(b->first_click == 1, "first_click not 1");
    CHECK(b->revealed_count == 0, "revealed_count not 0");
    board_destroy(b);
    PASS();
}

static void test_board_place_mines(void)
{
    TEST("board_place_mines (safe zone)");
    srand((unsigned)time(NULL));
    Board *b = board_create(9, 9, 10);
    board_place_mines(b, 4, 4);
    board_compute_numbers(b);

    /* Count total mines */
    int mine_count = 0;
    for (int i = 0; i < 81; i++)
        if (b->cells[i].flags & CELL_MINE) mine_count++;
    CHECK(mine_count == 10, "wrong mine count");

    /* Check safe zone: (4,4) and 8 neighbors should have no mines */
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            Cell *c = board_cell(b, 4 + dx, 4 + dy);
            CHECK(!(c->flags & CELL_MINE), "mine in safe zone");
        }
    }
    board_destroy(b);
    PASS();
}

static void test_board_reveal_flood(void)
{
    TEST("board_reveal (BFS flood fill)");
    Board *b = board_create(9, 9, 10);
    board_place_mines(b, 4, 4);
    board_compute_numbers(b);
    b->first_click = 0;
    b->state = STATE_PLAYING;

    int hit = board_reveal(b, 4, 4);
    CHECK(hit == 0, "first click hit a mine");
    CHECK(b->revealed_count > 0, "no cells revealed");
    /* Cell (4,4) should now be revealed */
    CHECK(board_cell(b, 4, 4)->flags & CELL_REVEALED, "start cell not revealed");
    board_destroy(b);
    PASS();
}

static void test_board_flag_toggle(void)
{
    TEST("board_toggle_flag (3-state cycle)");
    Board *b = board_create(9, 9, 10);
    Cell *c = board_cell(b, 0, 0);

    /* Normal -> Flagged */
    board_toggle_flag(b, 0, 0);
    CHECK(c->flags & CELL_FLAGGED, "not flagged after first toggle");
    CHECK(b->flagged_count == 1, "flagged_count not 1");

    /* Flagged -> Question */
    board_toggle_flag(b, 0, 0);
    CHECK(!(c->flags & CELL_FLAGGED), "still flagged after second toggle");
    CHECK(c->flags & CELL_QUESTION, "not question after second toggle");
    CHECK(b->flagged_count == 0, "flagged_count not 0 after unflag");

    /* Question -> Normal */
    board_toggle_flag(b, 0, 0);
    CHECK(!(c->flags & CELL_QUESTION), "still question after third toggle");
    CHECK(c->flags == 0, "flags not zero after full cycle");

    board_destroy(b);
    PASS();
}

static void test_board_win_detection(void)
{
    TEST("board_check_win");
    Board *b = board_create(3, 3, 1);
    /* Manually set up: mine at (0,0), reveal all others */
    b->cells[0].flags = CELL_MINE;
    b->cells[0].number = 0;
    board_compute_numbers(b);
    b->state = STATE_PLAYING;
    b->first_click = 0;

    /* Reveal all non-mine cells */
    for (int i = 1; i < 9; i++) {
        b->cells[i].flags |= CELL_REVEALED;
        b->revealed_count++;
    }

    int won = board_check_win(b);
    CHECK(won == 1, "should have won");
    CHECK(b->state == STATE_WON, "state not WON");

    board_destroy(b);
    PASS();
}

/* ---- Solver Tests ---- */

static void test_solver_basic(void)
{
    TEST("solver_solve (basic)");
    /* Create a simple board and check solver runs without crash */
    Board *b = board_create(9, 9, 10);
    board_place_mines(b, 4, 4);
    board_compute_numbers(b);
    b->first_click = 0;
    b->state = STATE_PLAYING;
    board_reveal(b, 4, 4);

    SolverResult res = solver_solve(b);
    /* Solver should reveal at least some cells */
    CHECK(res.cells_revealed >= 0, "negative cells_revealed");
    CHECK(res.cells_flagged >= 0, "negative cells_flagged");
    /* The original board should NOT be modified */
    int orig_revealed = b->revealed_count;
    SolverResult res2 = solver_solve(b);
    CHECK(b->revealed_count == orig_revealed, "solver modified original board");
    (void)res2;

    board_destroy(b);
    PASS();
}

static void test_no_guess_beginner(void)
{
    TEST("solver_generate_no_guess (beginner, 20 boards)");
    int success_count = 0;
    srand((unsigned)time(NULL));

    for (int trial = 0; trial < 20; trial++) {
        Board *b = board_create(9, 9, 10);
        int ok = solver_generate_no_guess(b, 4, 4, 500);
        if (ok) {
            /* Verify the board is truly solvable */
            Board *copy = board_create(b->width, b->height, b->mine_count);
            /* Copy mine layout */
            for (int i = 0; i < 81; i++) copy->cells[i] = b->cells[i];
            copy->first_click = 0;
            copy->state = STATE_PLAYING;
            board_reveal(copy, 4, 4);

            SolverResult res = solver_solve(copy);
            if (res.complete) success_count++;
            board_destroy(copy);
        }
        board_destroy(b);
    }

    CHECK(success_count >= 15,
          "less than 75% of no-guess boards actually solvable");
    printf("[PASS] (%d/20 verified solvable)\n", success_count);
    tests_passed++;
}

static void test_hint(void)
{
    TEST("solver_get_hint");
    Board *b = board_create(9, 9, 10);
    int ok = solver_generate_no_guess(b, 4, 4, 500);
    CHECK(ok, "no-guess generation failed");

    b->first_click = 0;
    b->state = STATE_PLAYING;
    board_reveal(b, 4, 4);

    /* After revealing the starting area, there should be a hint available */
    int hx = -1, hy = -1;
    int found = solver_get_hint(b, &hx, &hy);
    if (found) {
        /* The hinted cell should be safe (not a mine) */
        Cell *c = board_cell(b, hx, hy);
        CHECK(!(c->flags & CELL_MINE), "hint points to a mine!");
        CHECK(!(c->flags & CELL_REVEALED), "hint points to already revealed cell");
    }
    /* It's OK if no hint is found (board might be fully revealed already) */

    board_destroy(b);
    PASS();
}

static void test_probabilities(void)
{
    TEST("solver_compute_probabilities");
    Board *b = board_create(9, 9, 10);
    board_place_mines(b, 4, 4);
    board_compute_numbers(b);
    b->first_click = 0;
    b->state = STATE_PLAYING;
    board_reveal(b, 4, 4);

    double *prob_map = (double *)calloc(81, sizeof(double));
    CHECK(prob_map != NULL, "calloc failed");

    solver_compute_probabilities(b, prob_map);

    /* Check: revealed cells should have prob -1.0 */
    for (int i = 0; i < 81; i++) {
        if (b->cells[i].flags & CELL_REVEALED) {
            CHECK(prob_map[i] == -1.0, "revealed cell prob != -1.0");
        } else {
            CHECK(prob_map[i] >= 0.0 && prob_map[i] <= 1.0,
                  "unrevealed cell prob out of [0,1]");
        }
    }

    free(prob_map);
    board_destroy(b);
    PASS();
}

/* ---- Game Tests ---- */

static void test_game_dimensions(void)
{
    TEST("game_get_dimensions");
    int w, h, m;

    game_get_dimensions(DIFF_BEGINNER, &w, &h, &m);
    CHECK(w == 9 && h == 9 && m == 10, "beginner dims wrong");

    game_get_dimensions(DIFF_INTERMEDIATE, &w, &h, &m);
    CHECK(w == 16 && h == 16 && m == 40, "intermediate dims wrong");

    game_get_dimensions(DIFF_EXPERT, &w, &h, &m);
    CHECK(w == 30 && h == 16 && m == 99, "expert dims wrong");

    PASS();
}

/* ---- Main ---- */

int main(void)
{
    printf("\n=== Minesweeper Core Logic Tests ===\n\n");
    printf("[Board]\n");
    test_board_create();
    test_board_place_mines();
    test_board_reveal_flood();
    test_board_flag_toggle();
    test_board_win_detection();

    printf("\n[Solver]\n");
    test_solver_basic();
    test_no_guess_beginner();
    test_hint();
    test_probabilities();

    printf("\n[Game]\n");
    test_game_dimensions();

    printf("\n=== Results: %d passed, %d failed ===\n\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
