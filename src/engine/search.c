#include "engine/search.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/eval.h"
#include "engine/hash.h"
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

/* ── Transposition table ─────────────────────────────────────────────────
 * Keyed by hash_position(). Content-addressed, so entries stay valid
 * across searches/games — a matching key means an identical position,
 * regardless of when it was first stored. Always-replace on collision:
 * simple, and fine at this table size relative to branching factor. */
#define TT_SIZE (1 << 20) /* ~1M entries, ~24MB */

typedef enum { TT_EXACT, TT_ALPHA, TT_BETA } TTFlag;

typedef struct {
    U64    key;
    int    depth;
    int    score;
    TTFlag flag;
    Move   best;
} TTEntry;

static TTEntry *tt = NULL;

static void tt_ensure(void) {
    if (!tt) tt = calloc(TT_SIZE, sizeof(TTEntry));
}

static TTEntry *tt_probe(U64 key) {
    if (!tt) return NULL;
    TTEntry *e = &tt[key % TT_SIZE];
    return (e->key == key) ? e : NULL;
}

/* Mate scores are relative to the ply they were found at, so caching them
 * verbatim and reusing at a different ply would report the wrong mate
 * distance. Simplest safe fix: just don't cache them. */
static int is_mate_score(int s) {
    return s > MATE_SCORE - MAX_DEPTH || s < -(MATE_SCORE - MAX_DEPTH);
}

static void tt_store(U64 key, int depth, int score, TTFlag flag, Move best) {
    if (!tt || is_mate_score(score)) return;
    TTEntry *e = &tt[key % TT_SIZE];
    e->key = key; e->depth = depth; e->score = score; e->flag = flag; e->best = best;
}

/* Put the TT's remembered best move (if any) first, then sort the rest
 * captures-first. A verified-good move from a previous search is a much
 * stronger ordering hint than "is this a capture". */
static void order_moves(MoveList *ml, Move tt_move) {
    int start = 0;
    if (tt_move) {
        for (int i = 0; i < ml->count; i++) {
            if (ml->moves[i] == tt_move) {
                Move tmp = ml->moves[0];
                ml->moves[0] = ml->moves[i];
                ml->moves[i] = tmp;
                start = 1;
                break;
            }
        }
    }
    qsort(ml->moves + start, ml->count - start, sizeof(Move), cmp_moves);
}

/* Quiescence search caps how far it can run past the nominal search depth,
 * so a long forced sequence of checks/captures can't blow the stack. */
#define MAX_QUIESCENCE_PLY 16

/* Search only "noisy" moves (captures, promotions, and — while in check —
 * every legal move) until the position is quiet, then return a static
 * eval. This avoids the horizon effect: without it, alpha_beta() would
 * stop mid-capture-sequence at depth 0 and misjudge simple trades. */
static int quiescence(Position *pos, int alpha, int beta, int qply) {
    node_count++;

    int in_check = is_in_check(pos, pos->side);

    if (!in_check) {
        int stand_pat = evaluate(pos);
        if (stand_pat >= beta) return beta;
        if (stand_pat > alpha) alpha = stand_pat;
    }

    if (qply >= MAX_QUIESCENCE_PLY) return alpha;

    MoveList ml;
    generate_moves(pos, &ml);
    qsort(ml.moves, ml.count, sizeof(Move), cmp_moves);

    int legal = 0;
    for (int i = 0; i < ml.count; i++) {
        int flags = FLAGS(ml.moves[i]);
        /* When not in check, only look at noisy moves; when in check we
         * must consider every legal reply to find real evasions. */
        if (!in_check && !(flags & (FLAG_CAPTURE | FLAG_PROMOTION)))
            continue;

        Position saved;
        memcpy(&saved, pos, sizeof(Position));

        if (!make_move(pos, ml.moves[i])) {
            memcpy(pos, &saved, sizeof(Position));
            continue;
        }
        legal++;

        int score = -quiescence(pos, -beta, -alpha, qply + 1);
        memcpy(pos, &saved, sizeof(Position));

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    if (in_check && legal == 0)
        return -(MATE_SCORE - qply); /* checkmate found inside quiescence */

    return alpha;
}

static int alpha_beta(Position *pos, int depth, int alpha, int beta) {
    node_count++;

    if (depth == 0) return quiescence(pos, alpha, beta, 0);

    U64 key = hash_position(pos);
    Move tt_move = 0;
    TTEntry *hit = tt_probe(key);
    if (hit) {
        tt_move = hit->best;
        if (hit->depth >= depth) {
            if (hit->flag == TT_EXACT) return hit->score;
            if (hit->flag == TT_ALPHA && hit->score <= alpha) return alpha;
            if (hit->flag == TT_BETA  && hit->score >= beta)  return beta;
        }
    }

    MoveList ml;
    generate_moves(pos, &ml);
    order_moves(&ml, tt_move);

    int legal = 0;
    int orig_alpha = alpha;
    Move best_move = 0;

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

        if (score >= beta) {
            tt_store(key, depth, beta, TT_BETA, ml.moves[i]);
            return beta;
        }
        if (score > alpha) {
            alpha = score;
            best_move = ml.moves[i];
        }
    }

    if (legal == 0) {
        /* Checkmate or stalemate */
        int score = is_in_check(pos, pos->side)
            ? -(MATE_SCORE - (MAX_DEPTH - depth))
            : 0;
        return score;
    }

    tt_store(key, depth, alpha, (alpha > orig_alpha) ? TT_EXACT : TT_ALPHA, best_move);
    return alpha;
}

SearchResult search(Position *pos, int depth) {
    SearchResult res = {0, -INF, 0};
    node_count = 0;
    tt_ensure();

    MoveList ml;
    generate_moves(pos, &ml);
    U64 root_key = hash_position(pos);
    TTEntry *hit = tt_probe(root_key);
    order_moves(&ml, hit ? hit->best : 0);

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

    if (res.best_move)
        tt_store(root_key, depth, alpha, TT_EXACT, res.best_move);

    res.nodes = node_count;
    return res;
}
