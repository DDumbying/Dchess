#ifndef STATS_H
#define STATS_H

/* ─────────────────────────────────────────────────────────────
 * dchess  –  persistent game statistics
 *
 * Stats are stored in  ~/.local/share/dchess/stats.dat
 * (plain binary struct, versioned with a magic header).
 * ───────────────────────────────────────────────────────────── */

#define DCHESS_VERSION "1.0.0-alpha"

/* Maximum number of individual game records kept for the history graph */
#define DCHESS_MAX_HISTORY 256

/* One entry per completed game, stored chronologically */
typedef struct {
    long timestamp;   /* Unix epoch seconds when the game finished */
    int  result;      /* +1 win, 0 draw, -1 loss (from human perspective) */
} GameRecord;

typedef struct {
    /* per-difficulty counters  [0]=easy  [1]=medium  [2]=hard */
    int games_played[3];
    int wins[3];
    int losses[3];
    int draws[3];

    /* totals across all difficulties */
    int total_moves;          /* sum of all moves made */
    int total_time_secs;      /* cumulative play-time in seconds */
    int longest_game_moves;   /* record for single game move-count */

    /* side stats */
    int played_as_white;
    int played_as_black;
    int wins_as_white;
    int wins_as_black;

    /* per-game history for the rolling win-rate graph */
    int        history_count;
    GameRecord history[DCHESS_MAX_HISTORY];
} DchessStats;

/* Load stats from disk into *s.  Zeros *s and returns 0 on first run. */
int  stats_load(DchessStats *s);

/* Persist *s to disk.  Returns 0 on success. */
int  stats_save(const DchessStats *s);

/* Pretty-print to stdout (no ncurses). */
void stats_print(const DchessStats *s);

/* Record the result of one finished game.
 *   difficulty : 0=easy 1=medium 2=hard
 *   result     : 1=win  0=draw  -1=loss  (from human player perspective)
 *   player_side: WHITE(0) or BLACK(1)
 *   moves      : total half-moves played
 *   time_secs  : seconds elapsed
 */
void stats_record(DchessStats *s,
                  int difficulty,
                  int result,
                  int player_side,
                  int moves,
                  int time_secs);

#endif /* STATS_H */
