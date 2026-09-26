#ifndef RECORDS_H
#define RECORDS_H
#include "game/game.h"
#include "game/players.h"
#include "utils/engines.h"
#include "utils/stats.h"

typedef enum { KIND_PROFILE, KIND_GUEST, KIND_DCHESS, KIND_ENGINE } SideKind;

typedef struct {
    char     date[11], time[9];
    char     white[PLAYER_NAME_MAX + 1], black[PLAYER_NAME_MAX + 1];
    SideKind white_kind, black_kind;
    char     white_strength[32], black_strength[32];
    int      result;          /* 1 white won, 0 draw, -1 black won */
    char     end_reason[16];
    int      plies, seconds, legacy;
} Record;

typedef struct { Record *r; int count, cap; } RecordList;
typedef struct { int games, wins, draws, losses; } RecordTally;

int  records_path(char *buf, size_t n);
/* "checkmate", "stalemate", "material", "fifty-move", "repetition", "resigned" */
const char *records_end_reason(const char *result_text);
int  records_append(const char *path, const GameState *g, const Player p[2],
                    const EngineList *engines);
int  records_append_legacy(const char *path, const char *profile, long timestamp, int result);
int  records_load(const char *path, RecordList *out);   /* 0 if missing; skips bad games */
void records_free(RecordList *l);
/* Unix time of a record's Date and Time tags, or 0. */
long records_time(const Record *r);
int  records_rename(const char *path, const char *old_name, const char *new_name);

/* Legacy records are excluded; add the profile's legacy line yourself. */
RecordTally records_tally(const RecordList *l, const char *who);
int  records_recent(const RecordList *l, const char *who, const Record **out, int max);
int  records_winrate(const RecordList *l, const char *who, float *out, int max);
void records_to_stats(const RecordList *l, const char *who, DchessStats *out);
#endif
