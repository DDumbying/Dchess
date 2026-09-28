#ifndef PUZZLES_H
#define PUZZLES_H

#include "game/game.h"

/* Themes, as a bit mask; puzzle_theme_name() names each bit. */
enum { TH_MATE1 = 1 << 0, TH_MATE2 = 1 << 1, TH_MATE3 = 1 << 2, TH_MATE = 1 << 3, TH_FORK = 1 << 4,
       TH_PIN = 1 << 5, TH_SKEWER = 1 << 6, TH_DISCOVERED = 1 << 7, TH_HANGING = 1 << 8,
       TH_SACRIFICE = 1 << 9, TH_ENDGAME = 1 << 10, TH_PROMOTION = 1 << 11, TH_BACKRANK = 1 << 12,
       TH_DEFENSIVE = 1 << 13, TH_BITS = 14 };
#define TH_ANY_MATE (TH_MATE1 | TH_MATE2 | TH_MATE3 | TH_MATE)

#define PUZZLE_MAX   4096
#define PUZZLE_PLIES 8

/* A Lichess puzzle: the FEN before the opponent's move, then the moves
 * (UCI, space-separated), the opponent's first. */
typedef struct { const char *id, *fen, *moves; short rating; unsigned short themes; } PuzzleData;
extern const PuzzleData puzzle_data[];            /* sorted by rating */
extern const int puzzle_count;

const char *puzzle_theme_name(int bit);           /* "mate in 1", "fork", … */
int  puzzle_find(const char *id);                 /* index or -1 */

/* Solving one. A move is right if it is the next solution move or gives
 * mate; the opponent's replies play themselves. */
typedef enum { PZ_WRONG, PZ_RIGHT, PZ_SOLVED } PuzzleVerdict;
typedef struct { int index; Move line[PUZZLE_PLIES]; int len, next, failed, done; } Puzzle;
int  puzzle_start(Puzzle *p, int index, GameState *g);    /* the FEN, then the opponent's move */
PuzzleVerdict puzzle_try(Puzzle *p, GameState *g, Move m); /* WRONG leaves g unchanged */
int  puzzle_hint(Puzzle *p);                              /* the from-square; counts as a fail */
int  puzzle_show_step(Puzzle *p, GameState *g);           /* plays one move; 0 once done */

#endif
