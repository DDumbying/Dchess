/* The built-in engine driver.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <time.h>
#include "game/opponent.h"
#include "engine/fen.h"
#include "engine/move.h"
#include "utils/bitboard.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void nap(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

static int wait_result(Opponent *o, SearchResult *r, U64 *key, long budget_ms)
{
    long t0 = now_ms();
    while (now_ms() - t0 < budget_ms) {
        if (opponent_poll(o, r, key)) return 1;
        nap(5);
    }
    return 0;
}

static const char *START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

static void test_finds_mate(void)
{
    printf("== a search from start to result ==\n");
    Position pos;
    parse_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", &pos, NULL, NULL);

    SearchResult r;
    U64 key = 0;
    Opponent *o = opponent_builtin(3, 5000);
    check("the driver is created", o != NULL);
    check("poll before any start returns 0", !opponent_poll(o, &r, &key));
    check("start is accepted", opponent_start(o, &pos, 0x1234));
    check("a second start while busy is refused", !opponent_start(o, &pos, 0x9999));
    check("a result arrives", wait_result(o, &r, &key, 10000));
    check("it is the back-rank mate Ra1-a8", FROM(r.best_move) == 0 && TO(r.best_move) == 56);
    check("the key comes back with it", key == 0x1234);
    check("the driver is idle again", !opponent_poll(o, &r, &key));
    opponent_free(o);
}

static void test_cancel(void)
{
    printf("== cancel ==\n");
    Position pos;
    parse_fen(START, &pos, NULL, NULL);

    SearchResult r;
    U64 key;
    Opponent *o = opponent_builtin(20, 20000);
    opponent_start(o, &pos, 1);
    long t0 = now_ms();
    opponent_cancel(o);
    check("cancel straight after start returns within 1s", now_ms() - t0 < 1000);
    check("and leaves no result to poll", !opponent_poll(o, &r, &key));
    check("the driver can start again", opponent_start(o, &pos, 2));
    opponent_cancel(o);
    opponent_free(o);
    opponent_free(NULL);
    check("freeing NULL is harmless", 1);
}

static void test_stop(void)
{
    printf("== stop ==\n");
    Position pos;
    parse_fen(START, &pos, NULL, NULL);

    SearchResult r;
    U64 key = 0;
    Opponent *o = opponent_builtin(20, 20000);
    opponent_start(o, &pos, 3);
    nap(200);
    long t0 = now_ms();
    opponent_stop(o);
    check("stop returns within 1s", now_ms() - t0 < 1000);
    check("the next poll has a result", opponent_poll(o, &r, &key));
    check("with the key and a move", key == 3 && r.best_move != 0);
    opponent_free(o);
}

int main(void)
{
    init_attacks();

    test_finds_mate();
    test_cancel();
    test_stop();

    if (failures) {
        printf("\n%d opponent test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll opponent tests passed.\n");
    return 0;
}
