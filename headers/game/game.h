#ifndef GAME_H
#define GAME_H

#include "engine/board.h"
#include "engine/move.h"
#include <time.h>

/* ── The game itself ─────────────────────────────────────────────────────
 *
 * Everything needed to know what has been played and whether the game is
 * over: the position, the move log, the clocks, and the draw-rule
 * bookkeeping. Deliberately knows nothing about ncurses, threads, the
 * cursor, or the engine -- it is the model the TUI renders and the engine
 * plays into, and it can be exercised in a plain unit test.
 *
 * All of this used to live in TUIState alongside the cursor position and
 * the search thread, with the rules logic spread across tui.c and
 * commands.c. That is what allowed a move to be committed from two
 * different places with two subtly different implementations, and a
 * "new game" to be spelled out longhand in three.
 *
 * game_play() is the ONLY function that advances the position. Anything
 * that mutates `pos` behind its back will desync the move log, the
 * clocks and the repetition history from the board. */

/* Half-moves of move log and evaluation history kept for display. Games
 * longer than this keep playing correctly -- only the scrollback stops
 * growing. (Draw detection does NOT depend on this; see below.) */
#define MAX_MOVE_HISTORY 1024

/* Ring of recent position hashes, used only for threefold repetition.
 *
 * It can be a small fixed ring because a repetition can never span an
 * irreversible move: a pawn push or a capture makes every earlier
 * position unreachable forever. So the only positions worth comparing
 * against are the last `halfmove_clock` ones -- and halfmove_clock can
 * never exceed 99 here, because 100 triggers the 50-move draw first.
 * Anything above 100 is therefore headroom.
 *
 * This replaced a flat 256-entry array that simply stopped recording
 * once full, which silently switched threefold detection off for the
 * rest of a long game. */
#define GAME_REPETITION_WINDOW 128

typedef struct {
    Position pos;

    /* Move log, parallel arrays indexed 0..move_count-1 */
    char move_history[MAX_MOVE_HISTORY][8];  /* "e2e4", "e7e8q", ... */
    int  move_piece[MAX_MOVE_HISTORY];       /* piece index 0-11 that moved */
    int  move_time[MAX_MOVE_HISTORY];        /* seconds spent on that move */
    int  move_count;                         /* half-moves played */

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

    /* Engine evaluation after each half-move, in centipawns */
    int  eval_history[MAX_MOVE_HISTORY];
    int  eval_count;
} GameState;

/* Start a fresh game from the standard opening position. Clears the move
 * log, clocks, draw bookkeeping and outcome. */
void game_reset(GameState *g);

/* Replace the position from a FEN string. Returns 0 and leaves `g`
 * completely untouched if the FEN is malformed. Clears the move log and
 * outcome (but not the clocks -- a loaded position continues the
 * session's timing). */
int game_load_fen(GameState *g, const char *fen);

/* Find the legal move matching from/to among the side to move's moves.
 *
 * `promo` is a FLAG_PROMO_* flag, or 0 to mean "queen, if this is a
 * promotion at all" -- the default every caller wants. Returns 1 and
 * writes *out on success. Returns 0 if no such move exists or if it
 * would leave the mover's own king in check. */
int game_find_move(const GameState *g, int from, int to, int promo, Move *out);

/* Play `m` and advance the game: charges the mover's clock, updates the
 * halfmove clock, makes the move, records the position hash and appends
 * to the move log. `m` must have come from game_find_move() (i.e. be
 * known legal in the current position).
 *
 * This is the single point at which the position changes. */
void game_play(GameState *g, Move m);

/* Recompute game_over/result. Checks, in order: checkmate/stalemate,
 * insufficient material, the 50-move rule, threefold repetition. The
 * order matters -- a position can be checkmate with material that could
 * never mate again, and the material rule must beat the counting rules
 * that would take another 50 moves to reach the same verdict.
 * A no-op once the game is over. Call after every game_play(). */
void game_update_status(GameState *g);

/* Append an engine evaluation (centipawns, White-positive) to the log. */
void game_record_eval(GameState *g, int score_cp);

/* Piece index 0-11 occupying `sq`, or -1 if the square is empty. */
int game_piece_at(const GameState *g, int sq);

/* Hash of the current position, for comparing against a snapshot. */
U64 game_hash(const GameState *g);

#endif
