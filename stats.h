#ifndef STATS_H
#define STATS_H

#include "board.h"

#define MAX_LEADERBOARD 10

/* ---- Leaderboard entry ---- */
typedef struct {
    char name[32];
    int  time_seconds;
    int  year, month, day;
} LeaderEntry;

/* ---- Statistics ---- */
typedef struct {
    /* Per difficulty: 0=beginner, 1=intermediate, 2=expert */
    int games_played[3];
    int games_won[3];
    int best_time[3];
    LeaderEntry leaderboard[3][MAX_LEADERBOARD];
    int leader_count[3];
} Stats;

Stats *stats_create(void);
void   stats_destroy(Stats *s);

/* Load / save from binary file */
int    stats_load(Stats *s, const char *filename);
int    stats_save(Stats *s, const char *filename);

/* Record a game result */
void   stats_record_game(Stats *s, Difficulty diff, int won, int time_seconds);

/* Check if time qualifies for leaderboard. Returns rank (0-based) or -1. */
int    stats_check_leaderboard(Stats *s, Difficulty diff, int time_seconds);

/* Add to leaderboard (call after stats_check_leaderboard returns >= 0) */
void   stats_add_leaderboard(Stats *s, Difficulty diff, const char *name, int time_seconds);

#endif /* STATS_H */
