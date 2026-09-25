#include "engine/search.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/eval.h"
#include "engine/hash.h"
#include "utils/constants.h"
#include "utils/bitboard.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>

static long node_count;

/* Checked periodically, not every node. On expiry search_aborted latches
 * and search() returns the last iteration that finished cleanly. Depth 1
 * runs with the check disabled, so a legal move is always available.
 * Cancellation is NOT gated that way -- it fires at every depth so quit
 * stays responsive, which means a cancelled search can return an empty
 * best_move. Callers must handle it (see search.h). */
static int time_limited;
static int search_aborted;
static struct timespec search_deadline;

/* Cancellation
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

/* MVV-LVA: most valuable victim first, taken by the least valuable
 * attacker. Indexed by piece type (P N B R Q K). */
static const int ORDER_VALUE[6] = { 1, 3, 3, 5, 9, 20 };

static int victim_type(const Position *pos, Move m)
{
    if (FLAGS(m) & FLAG_ENPASSANT) return 0;   /* target square is empty */

    int to = TO(m);
    int first = (pos->side == WHITE) ? 6 : 0;   /* the opponent's pieces */
    for (int i = first; i < first + 6; i++)
        if (GET_BIT(pos->bitboards[i], to)) return i - first;
    return 0;
}

/* Quiet moves that recently caused a cutoff at the same distance from the
 * root, and how often each piece-to-square pair has done so anywhere.
 * Cleared at the start of every search(), kept across its iterations. */
static Move killers[MAX_DEPTH][2];
static int  history[12][64];

#define SCORE_TT        1000000
#define SCORE_KILLER_1     8000
#define SCORE_KILLER_2     7000
#define SCORE_HISTORY_MAX  6999   /* below the killers */

static int is_quiet(Move m)
{
    return !(FLAGS(m) & (FLAG_CAPTURE | FLAG_PROMOTION));
}

static int move_score(const Position *pos, Move m, int ply)
{
    int flags = FLAGS(m);
    int score = 0;

    if (flags & FLAG_CAPTURE) {
        int victim   = ORDER_VALUE[victim_type(pos, m)];
        int attacker = ORDER_VALUE[PIECE(m) % 6];
        score = 10000 + victim * 100 - attacker;
    }
    if (flags & FLAG_PROMO_Q) score += 9000;

    if (is_quiet(m) && ply >= 0) {
        if (m == killers[ply][0]) return SCORE_KILLER_1;
        if (m == killers[ply][1]) return SCORE_KILLER_2;
        int h = history[PIECE(m)][TO(m)];
        return h < SCORE_HISTORY_MAX ? h : SCORE_HISTORY_MAX;
    }

    return score;
}

static void record_cutoff(Move m, int depth, int ply)
{
    if (!is_quiet(m) || ply < 0 || ply >= MAX_DEPTH) return;

    if (killers[ply][0] != m) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = m;
    }
    history[PIECE(m)][TO(m)] += depth * depth;
}
/* Transposition table
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

void search_clear(void)
{
    if (tt) memset(tt, 0, (size_t)TT_SIZE * sizeof(TTEntry));
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
static void score_moves(const Position *pos, const MoveList *ml,
                        Move tt_move, int ply, int *score)
{
    for (int i = 0; i < ml->count; i++)
        score[i] = (ml->moves[i] == tt_move) ? SCORE_TT
                                             : move_score(pos, ml->moves[i], ply);
}

/* Bring the best remaining move to slot i and return it. Most nodes cut
 * off after a move or two, so sorting the whole list up front was mostly
 * wasted. Takes the FIRST of equal scores and rotates rather than swaps,
 * which yields exactly the order a stable sort would. */
static Move pick_move(MoveList *ml, int *score, int i)
{
    int best = i;
    for (int j = i + 1; j < ml->count; j++)
        if (score[j] > score[best]) best = j;

    Move m = ml->moves[best];
    int  v = score[best];
    for (int j = best; j > i; j--) {
        ml->moves[j] = ml->moves[j - 1];
        score[j]     = score[j - 1];
    }
    ml->moves[i] = m;
    score[i]     = v;
    return m;
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

    /* When not in check, only noisy moves are searched here -- so drop the
     * rest before paying to score them. In check, every evasion counts. */
    if (!in_check) {
        int n = 0;
        for (int i = 0; i < ml.count; i++)
            if (FLAGS(ml.moves[i]) & (FLAG_CAPTURE | FLAG_PROMOTION))
                ml.moves[n++] = ml.moves[i];
        ml.count = n;
    }

    int order[MAX_MOVES];
    score_moves(pos, &ml, 0, -1, order);

    int legal = 0;
    for (int i = 0; i < ml.count; i++) {
        Move m = pick_move(&ml, order, i);

        Position saved;
        memcpy(&saved, pos, sizeof(Position));

        if (!make_move(pos, m)) {
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

static int alpha_beta(Position *pos, int depth, int ply, int alpha, int beta) {
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
    int order[MAX_MOVES];
    score_moves(pos, &ml, tt_move, ply, order);

    int legal = 0;
    int orig_alpha = alpha;
    Move best_move = 0;

    for (int i = 0; i < ml.count; i++) {
        pick_move(&ml, order, i);   /* lands in ml.moves[i] */

        Position saved;
        memcpy(&saved, pos, sizeof(Position));

        if (!make_move(pos, ml.moves[i])) {
            memcpy(pos, &saved, sizeof(Position));
            continue;
        }
        legal++;

        int score = -alpha_beta(pos, depth-1, ply+1, -beta, -alpha);
        memcpy(pos, &saved, sizeof(Position));

        if (score >= beta) {
            record_cutoff(ml.moves[i], depth, ply);
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
    memset(killers, 0, sizeof(killers));
    memset(history, 0, sizeof(history));
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
        int order[MAX_MOVES];
        score_moves(pos, &ml, hit ? hit->best : 0, 0, order);

        int alpha = -INF, beta = INF;
        Move iter_best_move  = 0;
        int  iter_best_score = -INF;
        int  legal = 0;

        for (int i = 0; i < ml.count; i++) {
            pick_move(&ml, order, i);

            Position saved;
            memcpy(&saved, pos, sizeof(Position));

            if (!make_move(pos, ml.moves[i])) {
                memcpy(pos, &saved, sizeof(Position));
                continue;
            }
            legal++;

            int score = -alpha_beta(pos, depth - 1, 1, -beta, -alpha);
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
