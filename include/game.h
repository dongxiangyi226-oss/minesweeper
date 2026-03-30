#ifndef GAME_H
#define GAME_H

#include "board.h"

/* ---- Game context ---- */
typedef struct {
    Board *board;
    Difficulty difficulty;
    int custom_w, custom_h, custom_mines;
    int elapsed_seconds;
    int no_guess_mode;          /* 1 = no-guess generation enabled */
    int fog_mode;               /* 1 = fog of war challenge mode */
    int fog_radius;             /* visibility radius in fog mode */
    int heatmap_on;             /* 1 = probability heatmap overlay */
    int replay_on;              /* 1 = replay mode active */
    int hint_x, hint_y;        /* last hint cell (-1 if none) */
    int hint_active;            /* 1 = hint highlight visible */
    /* Chord press tracking (both-button visual feedback) */
    int press_cx, press_cy;     /* cell under chord press (-1 if none) */
    int press_active;           /* 1 = cells are visually pressed */
    /* Exploded mine tracking */
    int explode_x, explode_y;   /* the mine cell that was clicked (-1 if none) */
    /* Animation queue (cascade reveal, win/loss effects) */
    int    anim_active;         /* 1 = animation in progress, blocks input */
    int   *anim_cells;          /* flat indices to reveal, in BFS order */
    int    anim_count;          /* total cells in animation queue */
    int    anim_index;          /* next cell to reveal */
    int    anim_type;           /* 0=cascade, 1=win reveal, 2=loss mines */
    /* Keyboard cursor */
    int    cursor_x, cursor_y;  /* keyboard cursor position */
    int    cursor_visible;      /* 1 = show cursor */
    int    cursor_blink;        /* animation counter for pulse */
    /* AI auto-play */
    int    autoplay_active;     /* 1 = solver auto-playing */
    int    autoplay_hl_x, autoplay_hl_y;  /* highlighted "reason" cell */
    /* Toroidal mode (donut topology) */
    int    toroidal_mode;       /* 1 = edges wrap around */
} Game;

Game *game_create(void);
void  game_destroy(Game *g);

/* Start new game with given difficulty */
void  game_new(Game *g, Difficulty diff);
void  game_new_custom(Game *g, int w, int h, int mines);

/* Handle left click at cell (x,y) */
void  game_left_click(Game *g, int x, int y);

/* Handle right click at cell (x,y) */
void  game_right_click(Game *g, int x, int y);

/* Handle chord (middle click or both buttons) at cell (x,y) */
void  game_chord_click(Game *g, int x, int y);

/* Timer tick (call every second) */
void  game_tick(Game *g);

/* Toggle heatmap */
void  game_toggle_heatmap(Game *g);

/* Toggle fog of war */
void  game_toggle_fog(Game *g);

/* Request hint */
void  game_request_hint(Game *g);

/* Get board dimensions for current difficulty */
void  game_get_dimensions(Difficulty diff, int *w, int *h, int *mines);

#endif /* GAME_H */
