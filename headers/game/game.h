#ifndef GAME_H
#define GAME_H

#include "engine/board.h"
#include "engine/move.h"
#include "engine/fen.h"
#include <time.h>

/* The played game: position, move log, clocks and draw bookkeeping.
 * Knows nothing about ncurses, threads or the engine.
 *
 * game_play() is the ONLY function that advances the position; mutating
 * `pos` behind its back desyncs the log, clocks and repetition history. */

/* Display scrollback only. Draw detection does not depend on it. */
#define MAX_MOVE_HISTORY 1024

/* A repetition can never span an irreversible move, so only the last
 * halfmove_clock positions can match -- and that never exceeds 99 before
 * the 50-move rule fires. The rest is headroom. */
#define GAME_REPETITION_WINDOW 128

/* Snapshot rather than an unmake_move(): correct by construction for
 * castling, en passant and promotion. ~144 KB buys unlimited undo. */
typedef struct {
    Position pos;            /* as it was BEFORE the move */
    int      halfmove_clock;
} UndoRecord;

typedef struct {
    Position pos;

    /* Move log, parallel arrays indexed 0..move_count-1 */
    char move_history[MAX_MOVE_HISTORY][8];  /* SAN: "e4", "Nxd5", "O-O" */
    Move move_made[MAX_MOVE_HISTORY];        /* the move itself */
    int  move_piece[MAX_MOVE_HISTORY];       /* piece index 0-11 that moved */
    int  move_time[MAX_MOVE_HISTORY];        /* seconds spent on that move */
    int  move_count;                         /* half-moves played */
    int  log_start;                          /* first ply actually logged; a
                                                FEN starting mid-game has
                                                no record of earlier ones */

    /* Draw detection */
    int  halfmove_clock;                     /* plies since pawn move/capture */
    U64  repetition[GAME_REPETITION_WINDOW]; /* ring of recent position hashes */
    int  position_count;                     /* positions recorded, monotonic */

    /* Clocks: total seconds spent by each side */
    int             white_clock;
    int             black_clock;
    time_t          turn_start;      /* when the current turn began */
    struct timespec turn_start_mono; /* high-res start, for centiseconds */
    int             clock_started;   /* clocks don't tick before move 1 */
    int             clock_side;      /* side whose clock is ticking */

    /* Outcome. result is non-empty exactly when game_over is set. */
    int  game_over;
    char result[64];

    /* Empty unless the game began somewhere other than the standard
     * position; PGN needs it to be readable. */
    char start_fen[FEN_BUFSIZE];

    /* Engine evaluation after each half-move, in centipawns */
    int  eval_history[MAX_MOVE_HISTORY];
    int  eval_ply[MAX_MOVE_HISTORY];   /* the move each one was recorded for */
    int  eval_count;

    /* Undo stack, one entry per ply played, parallel to the move log. */
    UndoRecord undo[MAX_MOVE_HISTORY];
    int        undo_count;
} GameState;

void game_reset(GameState *g);

/* Returns 0 and leaves `g` untouched on a malformed FEN. Keeps the clocks
 * running. */
int game_load_fen(GameState *g, const char *fen);

/* `promo` is a FLAG_PROMO_* flag, or 0 for "queen if this is a promotion
 * at all". Returns 0 if no such move exists or it leaves the king in
 * check. */
int game_find_move(const GameState *g, int from, int to, int promo, Move *out);

/* `m` must have come from game_find_move(). */
void game_play(GameState *g, Move m);

/* Call after every game_play(). Order matters: mate can happen with
 * material that could never mate again, and the material rule must beat
 * the counting rules that would reach the same verdict 50 moves later. */
void game_update_status(GameState *g);

void game_record_eval(GameState *g, int score_cp);

/* The last move played, or 0 if none has been since the game or FEN
 * began. */
int game_last_move(const GameState *g, Move *m);

/* The most recent evaluation still in the history. Returns 0 if none. */
int game_last_eval(const GameState *g, int *score_cp);

/* Piece index 0-11 on `sq`, or -1. */
int game_piece_at(const GameState *g, int sq);

U64 game_hash(const GameState *g);

/* Search scores are from the side to move's point of view. */
int eval_white_view(int score_cp, int side_to_move);

int game_can_undo(const GameState *g);

/* Takes back exactly ONE ply and clears any game-over verdict. How many
 * plies a takeback should be depends on whether an engine plays the
 * other side, which this module does not know -- see tui_undo(). */
int game_undo(GameState *g);

#endif
