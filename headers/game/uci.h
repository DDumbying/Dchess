#ifndef UCI_H
#define UCI_H

#include "game/game.h"
#include "game/opponent.h"
#include "utils/engines.h"
#include <stddef.h>

/* Room for "position fen ..." plus MAX_MOVE_HISTORY moves. */
#define UCI_COMMAND_MAX 8448

/* What an "info" line said. Fields it did not mention are 0. */
typedef struct {
    int  depth;
    int  has_score;
    int  score_cp;
    int  is_mate;
    int  mate_in;     /* moves; negative when the side to move is mated */
    long nodes;
    long nps;
} UciInfo;

int  uci_parse_info(const char *line, UciInfo *out);

/* "(none)" and a bare "bestmove" give "". */
int  uci_parse_bestmove(const char *line, char *move, size_t n);

/* min and max are 0 when the option gives none. */
int  uci_parse_option(const char *line, char *name, size_t n, int *min, int *max);

/* "position startpos|fen <start> [moves ...]" from the game log. */
void uci_position_command(const GameState *g, char *buf, size_t n);

/* Centipawns from the side to move's view; a mate is a large score, the
 * way search() reports one. */
int  uci_info_score(const UciInfo *info);

#define UCI_HANDSHAKE_TIMEOUT_MS 10000
#define UCI_PROBE_TIMEOUT_MS      5000
#define UCI_STOP_TIMEOUT_MS       2000

typedef struct {
    char name[64];
    char author[64];
    int  elo_supported, elo_min, elo_max;
} UciProbe;

/* The engine starts on the first opponent_start(). The entry is copied. */
Opponent *opponent_uci(const EngineEntry *e);

/* Starts the engine, waits for "uciok" and quits it again. Blocks for at
 * most UCI_PROBE_TIMEOUT_MS. */
int  uci_probe(const char *path, UciProbe *out, char *err, size_t n);

#endif
