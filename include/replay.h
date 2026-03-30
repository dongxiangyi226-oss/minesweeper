#ifndef REPLAY_H
#define REPLAY_H

#include <stdint.h>
#include "board.h"

/* ---- Move actions ---- */
#define ACTION_LEFT   0
#define ACTION_RIGHT  1
#define ACTION_CHORD  2

/* ---- Move record ---- */
typedef struct {
    uint32_t tick_ms;       /* ms since game start */
    uint16_t x, y;
    uint8_t  action;
} MoveRecord;

/* ---- Replay data ---- */
typedef struct {
    int width, height, mine_count;
    int move_count;
    int move_capacity;
    MoveRecord *moves;
    uint32_t start_tick;    /* GetTickCount at game start */
} Replay;

Replay *replay_create(int w, int h, int mines);
void    replay_destroy(Replay *rp);
void    replay_reset(Replay *rp, int w, int h, int mines);

/* Record a move */
void    replay_record(Replay *rp, int x, int y, int action);

/* Save / load replay to file */
int     replay_save(Replay *rp, const char *filename);
Replay *replay_load(const char *filename);

#endif /* REPLAY_H */
