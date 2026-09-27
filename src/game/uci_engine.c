#include "game/uci_engine.h"
#include "game/book.h"
#include "game/game.h"
#include "game/timectl.h"
#include "engine/move.h"
#include "engine/search.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include "utils/stats.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *out_f;
static pthread_mutex_t out_lock = PTHREAD_MUTEX_INITIALIZER;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&out_lock);
    vfprintf(out_f, fmt, ap);
    fputc('\n', out_f);
    fflush(out_f);
    pthread_mutex_unlock(&out_lock);
    va_end(ap);
}

static void info(const SearchInfo *si)
{
    char line[256], mv[8];
    int n;
    if (si->score > MATE_BOUND || si->score < -MATE_BOUND) {
        int plies = MATE_SCORE - abs(si->score), moves = (plies + 1) / 2;
        n = snprintf(line, sizeof(line), "info depth %d score mate %d", si->depth, si->score > 0 ? moves : -moves);
    } else {
        n = snprintf(line, sizeof(line), "info depth %d score cp %d", si->depth, si->score);
    }
    n += snprintf(line + n, sizeof(line) - n, " nodes %ld time %ld pv", si->nodes, si->ms);
    for (int i = 0; i < si->pv_len && n < (int)sizeof(line) - 8; i++) {
        move_to_str(si->pv[i], mv);
        n += snprintf(line + n, sizeof(line) - n, " %s", mv);
    }
    say("%s", line);
}

/* The search thread: one "go". */
static struct {
    pthread_t       thread;
    int             running, infinite, stopped, done;
    pthread_mutex_t lock;
    pthread_cond_t  wake;
    Position        pos;
    int             depth, ms;
} job = { .lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER };

static void *search_thread(void *arg)
{
    (void)arg;
    SearchResult r = search(&job.pos, job.depth, job.ms);
    /* "go infinite" answers only after "stop". */
    pthread_mutex_lock(&job.lock);
    while (job.infinite && !job.stopped) pthread_cond_wait(&job.wake, &job.lock);
    pthread_mutex_unlock(&job.lock);
    char mv[8] = "0000";
    if (r.best_move) move_to_str(r.best_move, mv);
    say("bestmove %s", mv);
    pthread_mutex_lock(&job.lock);
    job.done = 1;
    pthread_mutex_unlock(&job.lock);
    return NULL;
}

static void stop_search(void)
{
    if (!job.running) return;
    /* search() clears a cancel as it starts, so keep asking until the
     * thread is done: a stop right after go must not be lost. */
    struct timespec nap = { 0, 1000000L };
    for (;;) {
        pthread_mutex_lock(&job.lock);
        job.stopped = 1;
        pthread_cond_signal(&job.wake);
        int done = job.done;
        pthread_mutex_unlock(&job.lock);
        if (done) break;
        search_cancel();
        nanosleep(&nap, NULL);
    }
    pthread_join(job.thread, NULL);
    job.running = 0;
}

static void set_position(GameState *g, char *args)
{
    char *moves = strstr(args, " moves");
    if (moves) *moves = '\0';
    game_reset(g);
    if (!strncmp(args, "fen ", 4)) game_load_fen(g, args + 4);
    if (!moves) return;
    char *save = NULL;
    for (char *t = strtok_r(moves + 6, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
        int from, to, promo;
        Move m;
        if (!parse_move_str(t, &from, &to, &promo) || !game_find_move(g, from, to, promo, &m)) break;
        game_play(g, m);
        game_update_status(g);
    }
}

static long field(const char *args, const char *key)
{
    const char *p = strstr(args, key);
    return p ? atol(p + strlen(key)) : -1;
}

static void go(GameState *g, const char *args, Book *book, int own_book, unsigned *rng)
{
    stop_search();
    Move bm = own_book && book ? book_pick(book, g, DIFF_HARD, rng) : 0;
    if (bm && !strstr(args, "infinite")) {
        char mv[8];
        move_to_str(bm, mv);
        say("bestmove %s", mv);
        return;
    }
    int side = g->pos.side;
    long depth = field(args, "depth "), movetime = field(args, "movetime ");
    long left = field(args, side == WHITE ? "wtime " : "btime ");
    long inc  = field(args, side == WHITE ? "winc " : "binc ");
    job.depth = depth > 0 ? (int)depth : MAX_DEPTH;
    job.ms = 0;
    if (movetime > 0)   job.ms = movetime > 30 ? (int)movetime - 20 : 10;
    else if (left >= 0) job.ms = tc_budget_ms(left, inc > 0 ? (int)inc : 0);
    job.infinite = strstr(args, "infinite") != NULL || (depth <= 0 && movetime <= 0 && left < 0);
    job.stopped = job.done = 0;
    job.pos = g->pos;
    job.running = pthread_create(&job.thread, NULL, search_thread, NULL) == 0;
}

int uci_engine_run(FILE *in, FILE *out)
{
    out_f = out;
    static GameState g;
    memset(&g, 0, sizeof(g));
    game_reset(&g);
    Book *book = book_builtin();
    int own_book = 1;
    unsigned rng = (unsigned)time(NULL);
    search_set_info(info);

    char line[8192];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *cmd = line + strspn(line, " \t");
        if (!strcmp(cmd, "uci")) {
            say("id name dchess %s", DCHESS_VERSION);
            say("id author DDumbying");
            say("option name OwnBook type check default true");
            say("uciok");
        } else if (!strcmp(cmd, "isready")) {
            say("readyok");
        } else if (!strncmp(cmd, "setoption ", 10)) {
            if (strstr(cmd, "name OwnBook value")) own_book = strstr(cmd, "value true") != NULL;
        } else if (!strcmp(cmd, "ucinewgame")) {
            stop_search();
            search_clear();
        } else if (!strncmp(cmd, "position ", 9)) {
            stop_search();
            set_position(&g, cmd + 9);
        } else if (!strncmp(cmd, "go", 2) && (cmd[2] == ' ' || !cmd[2])) {
            go(&g, cmd + 2, book, own_book, &rng);
        } else if (!strcmp(cmd, "stop")) {
            stop_search();
        } else if (!strcmp(cmd, "quit")) {
            break;
        }
    }
    stop_search();
    search_set_info(NULL);
    book_free(book);
    return 0;
}
