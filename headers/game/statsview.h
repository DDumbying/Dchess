#ifndef STATSVIEW_H
#define STATSVIEW_H

#include "game/profiles.h"
#include "game/records.h"

#define SV_TREND   60
#define SV_OPP_MAX 64

typedef struct {
    char name[PLAYER_NAME_MAX + 1];   /* "dchess Hard", an engine, a person, "Guest",
                                         or "before profiles" */
    int  games, wins, draws, losses;
    long last;                        /* records_time of the latest game; 0 if none */
} SvOpponent;

/* Everything the stats page shows for one profile. */
typedef struct {
    RecordTally total;                   /* includes the legacy line */
    RecordTally as_white, as_black;      /* recorded games only */
    float trend[SV_TREND]; int trend_count;   /* rolling win rate, oldest first */
    char  streak;  int streak_len;       /* 'W', 'L', 'D' or 0 */
    int   best_win_streak;
    int   per_week[4];                   /* [0] = the last 7 days, then older weeks */
    SvOpponent opp[SV_OPP_MAX]; int opp_count;   /* by games, most first */
    int   mates, resigns, stalemates, repetitions, fifty, material;
    int   avg_plies, avg_seconds;        /* recorded games only */
    const Record *recent[256]; int recent_count; /* newest first; points into the list */
} StatsView;

void stats_view_build(const RecordList *l, const Profile *p, long now, StatsView *out);

#endif
