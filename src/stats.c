/*
 * stats.c -- Game statistics and leaderboard management
 *
 * Binary file format: raw Stats struct written with fwrite/fread.
 * Leaderboard entries are sorted by time_seconds ascending (fastest first).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "stats.h"

/* ------------------------------------------------------------------ */
/*  Create / destroy                                                  */
/* ------------------------------------------------------------------ */

Stats *stats_create(void)
{
    Stats *s = (Stats *)calloc(1, sizeof(Stats));
    if (!s) return NULL;

    /* Initialize best_time to a large sentinel (no record yet) */
    for (int i = 0; i < 3; i++) {
        s->best_time[i] = 9999;
    }
    return s;
}

void stats_destroy(Stats *s)
{
    free(s);
}

/* ------------------------------------------------------------------ */
/*  Load / save (binary, entire struct)                               */
/* ------------------------------------------------------------------ */

int stats_load(Stats *s, const char *filename)
{
    if (!s || !filename) return 0;

    FILE *fp = fopen(filename, "rb");
    if (!fp) return 0;

    size_t n = fread(s, sizeof(Stats), 1, fp);
    fclose(fp);

    if (n != 1) {
        /* Reset to clean state on read failure */
        memset(s, 0, sizeof(Stats));
        for (int i = 0; i < 3; i++)
            s->best_time[i] = 9999;
        return 0;
    }

    /* Clamp leader_count to valid range */
    for (int i = 0; i < 3; i++) {
        if (s->leader_count[i] < 0) s->leader_count[i] = 0;
        if (s->leader_count[i] > MAX_LEADERBOARD) s->leader_count[i] = MAX_LEADERBOARD;
    }

    return 1;
}

int stats_save(Stats *s, const char *filename)
{
    if (!s || !filename) return 0;

    FILE *fp = fopen(filename, "wb");
    if (!fp) return 0;

    size_t n = fwrite(s, sizeof(Stats), 1, fp);
    fclose(fp);

    return (n == 1) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Record a game result                                              */
/* ------------------------------------------------------------------ */

void stats_record_game(Stats *s, Difficulty diff, int won, int time_seconds)
{
    if (!s) return;
    if (diff < 0 || diff > 2) return;  /* Only beginner/intermediate/expert */

    int d = (int)diff;

    s->games_played[d]++;

    if (won) {
        s->games_won[d]++;
        if (time_seconds < s->best_time[d]) {
            s->best_time[d] = time_seconds;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Leaderboard                                                       */
/* ------------------------------------------------------------------ */

/*
 * Check if time_seconds qualifies for the leaderboard.
 * Returns insertion rank (0-based) or -1 if it does not qualify.
 */
int stats_check_leaderboard(Stats *s, Difficulty diff, int time_seconds)
{
    if (!s) return -1;
    if (diff < 0 || diff > 2) return -1;

    int d = (int)diff;
    int count = s->leader_count[d];

    /* Find insertion position (sorted ascending by time) */
    int pos;
    for (pos = 0; pos < count; pos++) {
        if (time_seconds < s->leaderboard[d][pos].time_seconds) {
            break;
        }
    }

    /* Qualifies if there is room or it beats an existing entry */
    if (pos < MAX_LEADERBOARD) {
        return pos;
    }

    return -1;
}

/*
 * Add an entry to the leaderboard at the correct sorted position.
 * Shifts slower entries down; drops the last entry if the board is full.
 */
void stats_add_leaderboard(Stats *s, Difficulty diff, const char *name, int time_seconds)
{
    if (!s || !name) return;
    if (diff < 0 || diff > 2) return;

    int d = (int)diff;
    int pos = stats_check_leaderboard(s, diff, time_seconds);
    if (pos < 0) return;

    int count = s->leader_count[d];

    /* Shift entries from pos onward down by one */
    int last = (count < MAX_LEADERBOARD) ? count : MAX_LEADERBOARD - 1;
    for (int i = last; i > pos; i--) {
        s->leaderboard[d][i] = s->leaderboard[d][i - 1];
    }

    /* Fill in the new entry */
    LeaderEntry *entry = &s->leaderboard[d][pos];
    memset(entry, 0, sizeof(LeaderEntry));
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->time_seconds = time_seconds;

    /* Current date */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) {
        entry->year  = t->tm_year + 1900;
        entry->month = t->tm_mon + 1;
        entry->day   = t->tm_mday;
    }

    /* Update count */
    if (count < MAX_LEADERBOARD) {
        s->leader_count[d] = count + 1;
    }
}
