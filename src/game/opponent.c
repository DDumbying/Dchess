#include "game/opponent_impl.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

int opponent_start(Opponent *o, const GameState *g)            { return o->ops->start(o, g); }
int opponent_poll(Opponent *o, SearchResult *out, U64 *key)    { return o->ops->poll(o, out, key); }
void opponent_stop(Opponent *o)                                { o->ops->stop(o); }
void opponent_cancel(Opponent *o)                              { o->ops->cancel(o); }
void opponent_free(Opponent *o)                                { if (o) o->ops->destroy(o); }
const char *opponent_error(const Opponent *o)                  { return o->ops->error(o); }

/* busy and threaded belong to the owning thread. ready and result are
 * the worker's handoff and are guarded by mutex. */
typedef struct {
    Opponent        base;
    int             depth, time_ms;
    int             busy;
    int             threaded;
    pthread_t       thread;
    pthread_mutex_t mutex;
    int             ready;
    SearchResult    result;
    Position        snapshot;   /* the worker's private copy */
    U64             key;
} Builtin;

static void *worker(void *arg)
{
    Builtin *b = arg;
    SearchResult r = search(&b->snapshot, b->depth, b->time_ms);

    pthread_mutex_lock(&b->mutex);
    b->result = r;
    b->ready  = 1;
    pthread_mutex_unlock(&b->mutex);
    return NULL;
}

static int is_ready(Builtin *b)
{
    pthread_mutex_lock(&b->mutex);
    int r = b->ready;
    pthread_mutex_unlock(&b->mutex);
    return r;
}

static int builtin_start(Opponent *o, const GameState *g)
{
    Builtin *b = (Builtin *)o;
    if (b->busy) return 0;
    b->snapshot = g->pos;
    b->key      = game_hash(g);
    b->ready    = 0;
    b->busy     = 1;
    b->threaded = (pthread_create(&b->thread, NULL, worker, b) == 0);
    if (!b->threaded) worker(b);   /* no thread to be had: search here instead */
    return 1;
}

static void finish(Builtin *b)
{
    if (!b->threaded) return;
    /* search() clears any earlier cancel as it begins, so one sent before
     * the worker got that far would be lost. Keep asking until it answers. */
    while (!is_ready(b)) {
        search_cancel();
        struct timespec ms = { 0, 1000000L };
        nanosleep(&ms, NULL);
    }
    pthread_join(b->thread, NULL);
    b->threaded = 0;
}

static int builtin_poll(Opponent *o, SearchResult *out, U64 *key)
{
    Builtin *b = (Builtin *)o;
    if (!b->busy || !is_ready(b)) return 0;
    if (b->threaded) {
        pthread_join(b->thread, NULL);
        b->threaded = 0;
    }
    if (out) *out = b->result;
    if (key) *key = b->key;
    b->busy = 0;
    return 1;
}

static void builtin_stop(Opponent *o)
{
    Builtin *b = (Builtin *)o;
    if (b->busy) finish(b);
}

static void builtin_cancel(Opponent *o)
{
    Builtin *b = (Builtin *)o;
    if (!b->busy) return;
    finish(b);
    b->busy  = 0;
    b->ready = 0;
}

static void builtin_destroy(Opponent *o)
{
    Builtin *b = (Builtin *)o;
    builtin_cancel(o);
    pthread_mutex_destroy(&b->mutex);
    free(b);
}

static const char *builtin_error(const Opponent *o)
{
    (void)o;
    return NULL;
}

static const OpponentOps builtin_ops = {
    builtin_start, builtin_poll, builtin_stop,
    builtin_cancel, builtin_destroy, builtin_error,
};

Opponent *opponent_builtin(int depth, int time_ms)
{
    Builtin *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->base.ops = &builtin_ops;
    b->depth    = depth;
    b->time_ms  = time_ms;
    pthread_mutex_init(&b->mutex, NULL);
    return &b->base;
}
