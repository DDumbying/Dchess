#ifndef OPPONENT_IMPL_H
#define OPPONENT_IMPL_H

#include "game/opponent.h"

/* For driver implementations only. Each driver's struct starts with a
 * struct Opponent, so a pointer to one is a pointer to the other. */
typedef struct {
    int  (*start)(Opponent *o, const GameState *g);
    int  (*poll)(Opponent *o, SearchResult *out, U64 *key);
    void (*stop)(Opponent *o);
    void (*cancel)(Opponent *o);
    void (*destroy)(Opponent *o);
    const char *(*error)(const Opponent *o);
} OpponentOps;

struct Opponent { const OpponentOps *ops; };

#endif
