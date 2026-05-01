#include "engine/search.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/eval.h"
#include "utils/constants.h"
#include <string.h>
#include <stdlib.h>

static long node_count;

static int move_score(Move m) {
    int flags = FLAGS(m);
    if (flags & FLAG_CAPTURE) return 10000;
    if (flags & FLAG_PROMOTION) return 9000;
    return 0;
}

static int cmp_moves(const void *a, const void *b) {
    return move_score(*(Move*)b) - move_score(*(Move*)a);
}

static int alpha_beta(Position *pos, int depth, int alpha, int beta) {
    node_count++;

    if (depth == 0) return evaluate(pos);

    MoveList ml;
    generate_moves(pos, &ml);

    /* Sort: captures first */
    qsort(ml.moves, ml.count, sizeof(Move), cmp_moves);

    int legal = 0;
    for (int i = 0; i < ml.count; i++) {
        Position saved;
        memcpy(&saved, pos, sizeof(Position));

        if (!make_move(pos, ml.moves[i])) {
            memcpy(pos, &saved, sizeof(Position));
            continue;
        }
        legal++;

        int score = -alpha_beta(pos, depth-1, -beta, -alpha);
        memcpy(pos, &saved, sizeof(Position));

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    if (legal == 0) {
        /* Checkmate or stalemate */
        if (is_in_check(pos, pos->side))
            return -(MATE_SCORE - (MAX_DEPTH - depth));
        return 0; /* stalemate */
    }

    return alpha;
}

SearchResult search(Position *pos, int depth) {
    SearchResult res = {0, -INF, 0};
    node_count = 0;

    MoveList ml;
    generate_moves(pos, &ml);
    qsort(ml.moves, ml.count, sizeof(Move), cmp_moves);

    int alpha = -INF, beta = INF;

    for (int i = 0; i < ml.count; i++) {
        Position saved;
        memcpy(&saved, pos, sizeof(Position));

        if (!make_move(pos, ml.moves[i])) {
            memcpy(pos, &saved, sizeof(Position));
            continue;
        }

        int score = -alpha_beta(pos, depth-1, -beta, -alpha);
        memcpy(pos, &saved, sizeof(Position));

        if (score > alpha) {
            alpha = score;
            res.best_move  = ml.moves[i];
            res.best_score = score;
        }
    }

    res.nodes = node_count;
    return res;
}
