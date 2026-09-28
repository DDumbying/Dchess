/* dchess as a UCI engine, driven over pipes like a GUI would.
 *
 * Build & run:  make test
 */
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "game/uci_engine.h"
#include "game/game.h"
#include "engine/move.h"
#include "utils/bitboard.h"
#include "utils/version.h"

static int failures = 0;
static int to_engine, from_engine;
static FILE *engine_in, *engine_out;
static char pending[16384];
static size_t pending_len;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void *engine(void *arg)
{
    (void)arg;
    uci_engine_run(engine_in, engine_out);
    fclose(engine_out);
    return NULL;
}

static void send(const char *cmd)
{
    if (write(to_engine, cmd, strlen(cmd)) < 0 || write(to_engine, "\n", 1) < 0) perror("write");
}

/* The next line starting with `prefix` within `ms`, copied to `out`. */
static int expect(const char *prefix, int ms, char *out, size_t n)
{
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        char *nl;
        while ((nl = memchr(pending, '\n', pending_len))) {
            size_t len = (size_t)(nl - pending);
            int match = !strncmp(pending, prefix, strlen(prefix));
            if (match && out) snprintf(out, n, "%.*s", (int)len, pending);
            memmove(pending, nl + 1, pending_len - len - 1);
            pending_len -= len + 1;
            if (match) return 1;
        }
        clock_gettime(CLOCK_MONOTONIC, &t);
        long left = ms - ((t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000);
        if (left <= 0) return 0;
        struct pollfd p = { from_engine, POLLIN, 0 };
        if (poll(&p, 1, (int)left) <= 0) return 0;
        ssize_t got = read(from_engine, pending + pending_len, sizeof(pending) - pending_len - 1);
        if (got <= 0) return 0;
        pending_len += (size_t)got;
    }
}

/* Is `mv` legal after `fen` (or the start) and `moves`? */
static int legal_after(const char *fen, const char *moves, const char *mv)
{
    static GameState g;
    memset(&g, 0, sizeof(g));
    game_reset(&g);
    if (fen) game_load_fen(&g, fen);
    char buf[256], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", moves ? moves : "");
    int from, to, promo;
    Move m;
    for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save))
        if (parse_move_str(t, &from, &to, &promo) && game_find_move(&g, from, to, promo, &m)) game_play(&g, m);
    return parse_move_str(mv, &from, &to, &promo) && game_find_move(&g, from, to, promo, &m);
}

int main(void)
{
    init_attacks();
    int a[2], b[2];
    if (pipe(a) || pipe(b)) { perror("pipe"); return 1; }
    engine_in = fdopen(a[0], "r");
    to_engine = a[1];
    engine_out = fdopen(b[1], "w");
    from_engine = b[0];
    pthread_t th;
    pthread_create(&th, NULL, engine, NULL);
    char line[1024];

    printf("== handshake ==\n");
    send("uci");
    check("the engine names itself with the version", expect("id name", 2000, line, sizeof(line)) &&
                                                     !strcmp(line, "id name dchess " DCHESS_VERSION));
    check("uci is answered with uciok", expect("uciok", 2000, NULL, 0));
    send("isready");
    check("isready with readyok", expect("readyok", 2000, NULL, 0));
    send("setoption name OwnBook value false");

    printf("== searching ==\n");
    send("position startpos moves e2e4 e7e5");
    send("go depth 4");
    check("info lines report the depth", expect("info depth", 5000, NULL, 0));
    check("then a legal bestmove", expect("bestmove", 5000, line, sizeof(line)) &&
                                   legal_after(NULL, "e2e4 e7e5", line + 9));
    send("position fen 6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    send("go depth 4");
    check("a mate is reported in moves", expect("info depth 4 score mate 1", 5000, NULL, 0));
    check("and played", expect("bestmove a1a8", 5000, NULL, 0));

    printf("== awkward moves ==\n");
    send("position fen r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1 moves e1g1 e8c8");
    send("go depth 2");
    check("castling moves are understood", expect("bestmove", 5000, line, sizeof(line)) &&
          legal_after("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1 e8c8", line + 9));
    send("position fen 8/P6k/8/8/8/8/8/K7 w - - 0 1 moves a7a8q");
    send("go depth 2");
    check("so are promotions", expect("bestmove", 5000, line, sizeof(line)) &&
          legal_after("8/P6k/8/8/8/8/8/K7 w - - 0 1", "a7a8q", line + 9));

    printf("== stopping ==\n");
    send("position startpos");
    send("go infinite");
    usleep(200000);
    send("stop");
    check("stop ends an infinite search", expect("bestmove", 1000, NULL, 0));
    send("go infinite");
    send("stop");
    check("even straight after go", expect("bestmove", 1000, NULL, 0));
    check("with exactly one bestmove", !expect("bestmove", 400, NULL, 0));
    send("go wtime 0 btime 60000");
    check("no time left still gives a move", expect("bestmove", 1500, NULL, 0));

    send("position startpos moves e2e4");
    send("go wtime 60000");
    check("only the other side's clock still gives a move", expect("bestmove", 2500, line, sizeof(line)) &&
                                                            strcmp(line, "bestmove 0000"));
    send("position fen 6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
    send("go nodes 20000");
    check("go nodes answers", expect("bestmove", 2500, NULL, 0));
    send("go mate 1");
    check("go mate answers", expect("bestmove a1a8", 2500, NULL, 0));

    send("quit");
    pthread_join(th, NULL);
    check("quit ends the engine", 1);

    if (failures) {
        printf("\n%d UCI engine test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll UCI engine tests passed.\n");
    return 0;
}
