#ifndef PUZZLE_STATS_H
#define PUZZLE_STATS_H

#include <stddef.h>
#include "game/puzzles.h"

/* A profile's puzzles: indices into puzzle_data. */
typedef struct {
    int rating, played, streak, best_streak, rush_best;
    unsigned char seen[PUZZLE_MAX];
    int missed[PUZZLE_MAX], nmissed;          /* oldest first */
    int pending;                              /* a rated puzzle left unfinished, or -1 */
} PuzzleStats;

void puzzle_stats_init(PuzzleStats *s);
int  puzzle_stats_dir(char *buf, size_t n);   /* ~/.local/share/dchess/puzzles, created */
int  puzzle_stats_can_save(const char *profile);   /* no '/', no leading '.', not empty */
/* 1 if read; 0 gives the defaults. Unknown ids and bad lines are skipped. */
int  puzzle_stats_load(const char *dir, const char *profile, PuzzleStats *s);
int  puzzle_stats_save(const char *dir, const char *profile, const PuzzleStats *s);
int  puzzle_stats_rename(const char *dir, const char *from, const char *to);
int  puzzle_stats_remove(const char *dir, const char *profile);

/* Elo against the puzzle's rating: K 40 for the first 20, then 20; never below 400. */
int  puzzle_rating_after(int rating, int played, int puzzle_rating, int solved);
void puzzle_stats_record(PuzzleStats *s, int index, int solved);   /* a rated result */
void puzzle_stats_forgive(PuzzleStats *s, int index);              /* solved outside Rated */
/* The next rated puzzle: the unfinished one, else a new one near the rating. */
int  puzzle_stats_next_rated(PuzzleStats *s, unsigned *seed);

/* An unseen puzzle near `target` (of `themes`, 0 = any), widening as needed,
 * then marked seen; once all of them are seen they start over. -1 if none has those themes. */
int  puzzle_pick(unsigned char seen[PUZZLE_MAX], int target, unsigned themes, unsigned *seed);

#endif
