/*
 * replay.c -- Record and replay Minesweeper games
 *
 * Binary file format:
 *   [4 bytes] magic "MSRP"
 *   [4 bytes] width
 *   [4 bytes] height
 *   [4 bytes] mine_count
 *   [4 bytes] move_count
 *   [move_count * sizeof(MoveRecord)] moves array
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "replay.h"

#define REPLAY_MAGIC "MSRP"
#define INITIAL_CAPACITY 256

/* ------------------------------------------------------------------ */
/*  Create / destroy / reset                                          */
/* ------------------------------------------------------------------ */

Replay *replay_create(int w, int h, int mines)
{
    Replay *rp = (Replay *)calloc(1, sizeof(Replay));
    if (!rp) return NULL;

    rp->width      = w;
    rp->height     = h;
    rp->mine_count = mines;
    rp->move_count = 0;
    rp->move_capacity = INITIAL_CAPACITY;
    rp->moves = (MoveRecord *)malloc(sizeof(MoveRecord) * rp->move_capacity);
    if (!rp->moves) {
        free(rp);
        return NULL;
    }
    rp->start_tick = GetTickCount();
    return rp;
}

void replay_destroy(Replay *rp)
{
    if (!rp) return;
    free(rp->moves);
    free(rp);
}

void replay_reset(Replay *rp, int w, int h, int mines)
{
    if (!rp) return;

    rp->width      = w;
    rp->height     = h;
    rp->mine_count = mines;
    rp->move_count = 0;

    /* Shrink back to initial capacity if it grew very large */
    if (rp->move_capacity > INITIAL_CAPACITY * 4) {
        MoveRecord *tmp = (MoveRecord *)realloc(rp->moves,
                            sizeof(MoveRecord) * INITIAL_CAPACITY);
        if (tmp) {
            rp->moves = tmp;
            rp->move_capacity = INITIAL_CAPACITY;
        }
    }

    rp->start_tick = GetTickCount();
}

/* ------------------------------------------------------------------ */
/*  Record a move                                                     */
/* ------------------------------------------------------------------ */

void replay_record(Replay *rp, int x, int y, int action)
{
    if (!rp) return;

    /* Grow the dynamic array when full (double capacity) */
    if (rp->move_count >= rp->move_capacity) {
        int new_cap = rp->move_capacity * 2;
        MoveRecord *tmp = (MoveRecord *)realloc(rp->moves,
                            sizeof(MoveRecord) * new_cap);
        if (!tmp) return;   /* allocation failed -- silently drop */
        rp->moves = tmp;
        rp->move_capacity = new_cap;
    }

    MoveRecord *m = &rp->moves[rp->move_count++];
    m->tick_ms = GetTickCount() - rp->start_tick;
    m->x       = (uint16_t)x;
    m->y       = (uint16_t)y;
    m->action  = (uint8_t)action;
}

/* ------------------------------------------------------------------ */
/*  Save replay to binary file                                        */
/* ------------------------------------------------------------------ */

int replay_save(Replay *rp, const char *filename)
{
    if (!rp || !filename) return 0;

    FILE *fp = fopen(filename, "wb");
    if (!fp) return 0;

    /* Magic */
    if (fwrite(REPLAY_MAGIC, 1, 4, fp) != 4) goto fail;

    /* Header fields */
    if (fwrite(&rp->width,      sizeof(int), 1, fp) != 1) goto fail;
    if (fwrite(&rp->height,     sizeof(int), 1, fp) != 1) goto fail;
    if (fwrite(&rp->mine_count, sizeof(int), 1, fp) != 1) goto fail;
    if (fwrite(&rp->move_count, sizeof(int), 1, fp) != 1) goto fail;

    /* Moves array */
    if (rp->move_count > 0) {
        size_t n = (size_t)rp->move_count;
        if (fwrite(rp->moves, sizeof(MoveRecord), n, fp) != n) goto fail;
    }

    fclose(fp);
    return 1;

fail:
    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Load replay from binary file                                      */
/* ------------------------------------------------------------------ */

Replay *replay_load(const char *filename)
{
    if (!filename) return NULL;

    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;

    /* Verify magic */
    char magic[4];
    if (fread(magic, 1, 4, fp) != 4 || memcmp(magic, REPLAY_MAGIC, 4) != 0) {
        fclose(fp);
        return NULL;
    }

    int w, h, mines, count;
    if (fread(&w,     sizeof(int), 1, fp) != 1) goto fail;
    if (fread(&h,     sizeof(int), 1, fp) != 1) goto fail;
    if (fread(&mines, sizeof(int), 1, fp) != 1) goto fail;
    if (fread(&count, sizeof(int), 1, fp) != 1) goto fail;

    /* Sanity checks */
    if (w <= 0 || h <= 0 || mines < 0 || count < 0 || count > 1000000) goto fail;

    Replay *rp = (Replay *)calloc(1, sizeof(Replay));
    if (!rp) goto fail;

    rp->width      = w;
    rp->height     = h;
    rp->mine_count = mines;
    rp->move_count = count;

    /* Allocate just enough (but at least INITIAL_CAPACITY) */
    rp->move_capacity = (count > INITIAL_CAPACITY) ? count : INITIAL_CAPACITY;
    rp->moves = (MoveRecord *)malloc(sizeof(MoveRecord) * rp->move_capacity);
    if (!rp->moves) {
        free(rp);
        goto fail;
    }

    if (count > 0) {
        size_t n = (size_t)count;
        if (fread(rp->moves, sizeof(MoveRecord), n, fp) != n) {
            free(rp->moves);
            free(rp);
            goto fail;
        }
    }

    rp->start_tick = 0;  /* Not meaningful for a loaded replay */
    fclose(fp);
    return rp;

fail:
    fclose(fp);
    return NULL;
}
