#include "engine/search.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/eval.h"
#include "engine/hash.h"
#include "utils/constants.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>

static long node_count;

/* ── Time budget ─────────────────────────────────────────────────────────
 * Checked periodically (not every node -- clock_gettime() isn't free)
 * from inside alpha_beta()/quiescence(). When the deadline passes,
 * search_aborted latches true and every frame unwinds immediately;
 * search()'s iterative-deepening loop then discards that in-progress
 * iteration and returns the last one that finished cleanly. Depth 1 is
 * always run with the time check disabled (see search()), so a legal
 * move is always available even under an unreasonably tight budget. */
static int time_limited;
static int search_aborted;
static struct timespec search_deadline;

/* ── Cancellation ────────────────────────────────────────────────────────
 * search_cancel() is the one function in this file meant to be called
 * from a *different* thread than the one running search() -- e.g. the
 * UI thread asking a background search to stop early (see commands.c).
 * That makes this the one piece of shared state in search.c that
 * actually needs a real cross-thread guarantee rather than the
 * single-threaded "only one search() in flight at a time" invariant
 * everything else here relies on, hence stdatomic.h instead of a plain
 * int like search_aborted above. */
static atomic_int cancel_requested;

void search_cancel(void) { atomic_store(&cancel_requested, 1); }

static int deadline_passed(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec  != search_deadline.tv_sec)
        return now.tv_sec > search_deadline.tv_sec;
    return now.tv_nsec >= search_deadline.tv_nsec;
}

static inline int time_check_due(void) { return (node_count & 2047) == 0; }
static inline int cancel_check_due(void) { return (node_count & 511) == 0; }

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

    if (search_aborted) return alpha; /* unwind quickly; result gets discarded */
    if (time_limited && time_check_due() && deadline_passed())
        search_aborted = 1;
    if (cancel_check_due() && atomic_load(&cancel_requested))
        search_aborted = 1;

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

    if (search_aborted) return alpha; /* unwind quickly; result gets discarded */
    if (time_limited && time_check_due() && deadline_passed())
        search_aborted = 1;
    if (cancel_check_due() && atomic_load(&cancel_requested))
        search_aborted = 1;

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

SearchResult search(Position *pos, int max_depth, int time_limit_ms) {
    SearchResult best = {0, -INF, 0, 0};
    node_count = 0;
    tt_ensure();
    search_aborted = 0;
    atomic_store(&cancel_requested, 0);

    if (time_limit_ms > 0) {
        clock_gettime(CLOCK_MONOTONIC, &search_deadline);
        search_deadline.tv_sec  += time_limit_ms / 1000;
        search_deadline.tv_nsec += (long)(time_limit_ms % 1000) * 1000000L;
        if (search_deadline.tv_nsec >= 1000000000L) {
            search_deadline.tv_sec  += 1;
            search_deadline.tv_nsec -= 1000000000L;
        }
    }

    for (int depth = 1; depth <= max_depth; depth++) {
        /* Depth 1 always runs uncapped, so `best` is never left empty --
         * see the header comment on search(). Every deeper iteration is
         * subject to the time budget, if one was given. */
        time_limited = (depth > 1) && (time_limit_ms > 0);

        MoveList ml;
        generate_moves(pos, &ml);
        U64 root_key = hash_position(pos);
        TTEntry *hit = tt_probe(root_key);
        order_moves(&ml, hit ? hit->best : 0);

        int alpha = -INF, beta = INF;
        Move iter_best_move  = 0;
        int  iter_best_score = -INF;
        int  legal = 0;

        for (int i = 0; i < ml.count; i++) {
            Position saved;
            memcpy(&saved, pos, sizeof(Position));

            if (!make_move(pos, ml.moves[i])) {
                memcpy(pos, &saved, sizeof(Position));
                continue;
            }
            legal++;

            int score = -alpha_beta(pos, depth - 1, -beta, -alpha);
            memcpy(pos, &saved, sizeof(Position));

            if (search_aborted) break; /* this iteration's numbers are unreliable */

            if (score > alpha) {
                alpha = score;
                iter_best_move  = ml.moves[i];
                iter_best_score = score;
            }
        }

        if (search_aborted) break; /* keep the previous (complete) iteration's `best` */

        best.best_move     = iter_best_move;
        best.best_score     = iter_best_score;
        best.depth_reached  = depth;
        if (iter_best_move)
            tt_store(root_key, depth, alpha, TT_EXACT, iter_best_move);

        if (legal == 0) break; /* checkmate/stalemate: nothing deeper to find */
        if (time_limit_ms > 0 && deadline_passed()) break;
    }

    best.nodes = node_count;
    return best;
}
