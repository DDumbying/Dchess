#ifndef SEARCH_H
#define SEARCH_H

#include "board.h"
#include "move.h"

typedef struct {
    Move best_move;
    int  best_score;
    long nodes;
} SearchResult;

SearchResult search(Position *pos, int depth);

#endif
