#include "game/opponent.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

/* busy and threaded belong to the owning thread. ready and result are
 * the worker's handoff and are guarded by mutex. */
struct Opponent {
    int             depth, time_ms;
    int             busy;
    int             threaded;
    pthread_t       thread;
    pthread_mutex_t mutex;
    int             ready;
    SearchResult    result;
    Position        snapshot;   /* the worker's private copy */
    U64             key;
};

Opponent *opponent_builtin(int depth, int time_ms)
{
    Opponent *o = calloc(1, sizeof(*o));
    if (!o) return NULL;
    o->depth   = depth;
    o->time_ms = time_ms;
    pthread_mutex_init(&o->mutex, NULL);
    return o;
}

static void *worker(void *arg)
{
    Opponent *o = arg;
    SearchResult r = search(&o->snapshot, o->depth, o->time_ms);

    pthread_mutex_lock(&o->mutex);
    o->result = r;
    o->ready  = 1;
    pthread_mutex_unlock(&o->mutex);
    return NULL;
}

static int is_ready(Opponent *o)
{
    pthread_mutex_lock(&o->mutex);
    int r = o->ready;
    pthread_mutex_unlock(&o->mutex);
    return r;
}

int opponent_start(Opponent *o, const Position *pos, U64 key)
{
    if (o->busy) return 0;
    o->snapshot = *pos;
    o->key      = key;
    o->ready    = 0;
    o->busy     = 1;
    o->threaded = (pthread_create(&o->thread, NULL, worker, o) == 0);
    if (!o->threaded) worker(o);   /* no thread to be had: search here instead */
    return 1;
}

static void finish(Opponent *o)
{
    if (!o->threaded) return;
    /* search() clears any earlier cancel as it begins, so one sent before
     * the worker got that far would be lost. Keep asking until it answers. */
    while (!is_ready(o)) {
        search_cancel();
        struct timespec ms = { 0, 1000000L };
        nanosleep(&ms, NULL);
    }
    pthread_join(o->thread, NULL);
    o->threaded = 0;
}

int opponent_poll(Opponent *o, SearchResult *out, U64 *key)
{
    if (!o->busy || !is_ready(o)) return 0;
    if (o->threaded) {
        pthread_join(o->thread, NULL);
        o->threaded = 0;
    }
    if (out) *out = o->result;
    if (key) *key = o->key;
    o->busy = 0;
    return 1;
}

void opponent_stop(Opponent *o)
{
    if (o->busy) finish(o);
}

void opponent_cancel(Opponent *o)
{
    if (!o->busy) return;
    finish(o);
    o->busy  = 0;
    o->ready = 0;
}

void opponent_free(Opponent *o)
{
    if (!o) return;
    opponent_cancel(o);
    pthread_mutex_destroy(&o->mutex);
    free(o);
}
