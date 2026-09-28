#ifndef REPLAY_H
#define REPLAY_H

#include "game/game.h"
#include "game/players.h"
#include "engine/fen.h"
#include <stddef.h>

typedef struct {
    char white[PLAYER_NAME_MAX + 1], black[PLAYER_NAME_MAX + 1];
    char result[8], date[11], event[64];
    long offset;                    /* of the game's first tag line */
} ReplayEntry;
typedef struct { ReplayEntry *e; int count, cap; } ReplayList;

typedef struct {
    ReplayEntry info;
    char fen[FEN_BUFSIZE];          /* start position; "" = standard */
    Move moves[MAX_MOVE_HISTORY];
    char san[MAX_MOVE_HISTORY][8];  /* each move as dchess writes it */
    int  count;
    char err[96];                   /* "" or "stopped at move 23: Qxh9" */
} ReplayGame;

/* Every game in a PGN file. 0 with a message on failure. */
int  replay_list(const char *path, ReplayList *out, char *err, size_t n);
void replay_list_free(ReplayList *l);

/* The game whose tags start at `offset`. A move that cannot be read stops
 * the game there, with out->err set; 0 only when the file cannot be read. */
/* The one legal move written `tok` in SAN (check marks and "=" optional), or 0. */
Move replay_find_san(const GameState *g, const char *tok);
int  replay_read(const char *path, long offset, ReplayGame *out, char *err, size_t n);

#endif
