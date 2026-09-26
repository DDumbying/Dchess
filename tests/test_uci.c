/* UCI protocol: parsers, position command, and (Task 4) the driver.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "game/uci.h"
#include "game/opponent.h"
#include "utils/engines.h"
#include "game/game.h"
#include "engine/move.h"
#include "utils/bitboard.h"
#include "utils/constants.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static int play(GameState *g, const char *text)
{
    int from, to, promo;
    Move m;
    if (!parse_move_str(text, &from, &to, &promo)) return 0;
    if (!game_find_move(g, from, to, promo, &m))   return 0;
    game_play(g, m);
    return 1;
}

static void test_info(void)
{
    printf("== info lines ==\n");
    UciInfo i;
    check("a full Stockfish line parses",
          uci_parse_info("info depth 18 seldepth 24 multipv 1 score cp 31 nodes 1234567 "
                         "nps 987654 hashfull 500 tbhits 0 time 1250 pv e2e4 e7e5 g1f3", &i) == 1);
    check("depth, score, nodes and nps",
          i.depth == 18 && i.has_score && !i.is_mate && i.score_cp == 31 &&
          i.nodes == 1234567 && i.nps == 987654);

    uci_parse_info("info depth 12 score mate -3 nodes 50 pv e1e2", &i);
    check("a mate score", i.has_score && i.is_mate && i.mate_in == -3);
    check("being mated is a large negative score", uci_info_score(&i) == -(MATE_SCORE - 3));
    uci_parse_info("info depth 9 score mate 2", &i);
    check("mating is a large positive score", uci_info_score(&i) == MATE_SCORE - 2);

    uci_parse_info("info depth 7 score cp -45 lowerbound nodes 900", &i);
    check("a bound after the score is skipped", i.score_cp == -45 && i.nodes == 900);
    check("a line without a score has none",
          uci_parse_info("info currmove e2e4 currmovenumber 1", &i) == 1 &&
          !i.has_score && i.depth == 0 && uci_info_score(&i) == 0);
    check("info string is ignored",
          uci_parse_info("info string NNUE evaluation enabled depth 99", &i) == 1 &&
          i.depth == 0);
    check("a bestmove line is not info", uci_parse_info("bestmove e2e4", &i) == 0);
    check("nor is a longer word", uci_parse_info("infoz depth 3", &i) == 0);
}

static void test_bestmove(void)
{
    printf("== bestmove ==\n");
    char m[16];
    check("with ponder", uci_parse_bestmove("bestmove e2e4 ponder e7e5", m, sizeof(m)) &&
                          strcmp(m, "e2e4") == 0);
    check("a promotion", uci_parse_bestmove("bestmove e7e8q", m, sizeof(m)) &&
                         strcmp(m, "e7e8q") == 0);
    check("(none) is empty", uci_parse_bestmove("bestmove (none)", m, sizeof(m)) && m[0] == '\0');
    check("a bare bestmove is empty", uci_parse_bestmove("bestmove", m, sizeof(m)) && m[0] == '\0');
    check("other lines are not bestmove", !uci_parse_bestmove("info depth 1", m, sizeof(m)));
}

static void test_option(void)
{
    printf("== option lines ==\n");
    char name[64];
    int lo, hi;
    check("UCI_Elo with a range",
          uci_parse_option("option name UCI_Elo type spin default 1320 min 1320 max 3190",
                           name, sizeof(name), &lo, &hi) &&
          strcmp(name, "UCI_Elo") == 0 && lo == 1320 && hi == 3190);
    check("a name with spaces",
          uci_parse_option("option name Skill Level type spin default 20 min 0 max 20",
                           name, sizeof(name), &lo, &hi) &&
          strcmp(name, "Skill Level") == 0 && lo == 0 && hi == 20);
    check("no range is 0..0",
          uci_parse_option("option name Ponder type check default false",
                           name, sizeof(name), &lo, &hi) &&
          strcmp(name, "Ponder") == 0 && lo == 0 && hi == 0);
    check("id lines are not options", !uci_parse_option("id name Stockfish", name, sizeof(name), &lo, &hi));
}

static void test_position(void)
{
    printf("== position command ==\n");
    char buf[UCI_COMMAND_MAX];
    GameState g;
    game_reset(&g);
    uci_position_command(&g, buf, sizeof(buf));
    check("the standard start", strcmp(buf, "position startpos") == 0);

    play(&g, "e2e4");
    play(&g, "e7e5");
    uci_position_command(&g, buf, sizeof(buf));
    check("with moves", strcmp(buf, "position startpos moves e2e4 e7e5") == 0);

    game_reset(&g);
    game_load_fen(&g, "8/P6k/8/8/8/8/8/K7 w - - 0 1");
    play(&g, "a7a8q");
    uci_position_command(&g, buf, sizeof(buf));
    check("a FEN start with a promotion",
          strcmp(buf, "position fen 8/P6k/8/8/8/8/8/K7 w - - 0 1 moves a7a8q") == 0);
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

static EngineEntry fake(int depth, int ms, int elo)
{
    EngineEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "Fake");
    snprintf(e.path, sizeof(e.path), "build/fake_uci");
    e.limit_depth = depth;
    e.limit_ms    = ms;
    e.elo         = elo;
    return e;
}

static void mode(const char *m) { setenv("FAKE_UCI_MODE", m, 1); }

static int no_children(void)
{
    return waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD;
}

static int is_legal(const GameState *g, Move m)
{
    Move found;
    return m && game_find_move(g, FROM(m), TO(m), FLAGS(m) & FLAG_PROMOTION, &found);
}

static int count(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)); p += strlen(needle)) n++;
    return n;
}

static void slurp(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t got = f ? fread(buf, 1, n - 1, f) : 0;
    buf[got] = '\0';
    if (f) fclose(f);
}

static void test_driver_normal(void)
{
    printf("== driver: a normal engine ==\n");
    mode("normal");
    GameState g;
    game_reset(&g);
    EngineEntry e = fake(0, 100, 0);
    SearchResult r;
    U64 key = 0;

    Opponent *o = opponent_uci(&e);
    check("the driver is created", o != NULL);
    check("poll before any start returns 0", !opponent_poll(o, &r, &key));
    check("start is accepted", opponent_start(o, &g));
    check("a second start while busy is refused", !opponent_start(o, &g));
    check("a result arrives", wait_result(o, &r, &key, 3000));
    check("it is a legal move", is_legal(&g, r.best_move));
    check("with the last info's depth, score and nodes",
          r.depth_reached == 2 && r.best_score == 15 && r.nodes == 400);
    check("and the game's key", key == game_hash(&g));
    check("no error", opponent_error(o) == NULL);

    game_play(&g, r.best_move);
    check("the same engine answers again",
          opponent_start(o, &g) && wait_result(o, &r, &key, 3000) && is_legal(&g, r.best_move));
    opponent_free(o);
    check("free leaves no child process", no_children());
}

static void test_driver_log(void)
{
    printf("== driver: what it sends ==\n");
    char logpath[] = "/tmp/dchess-uci-log-XXXXXX", buf[8192];
    int fd = mkstemp(logpath);
    close(fd);
    setenv("FAKE_UCI_LOG", logpath, 1);

    mode("elo");
    GameState g;
    game_reset(&g);
    play(&g, "e2e4");
    EngineEntry e = fake(0, 100, 1000);
    SearchResult r;
    U64 key;
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    game_play(&g, r.best_move);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    game_undo(&g);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    opponent_free(o);

    slurp(logpath, buf, sizeof(buf));
    check("limits strength when an Elo is set",
          strstr(buf, "setoption name UCI_LimitStrength value true") != NULL);
    check("with the Elo clamped to the engine's minimum",
          strstr(buf, "setoption name UCI_Elo value 1320") != NULL);
    check("sends the game with its moves", strstr(buf, "position startpos moves e2e4\n") != NULL);
    check("searches by time", strstr(buf, "go movetime 100") != NULL);
    check("ucinewgame first, and again after an undo", count(buf, "ucinewgame") == 2);
    check("quits on free", strstr(buf, "quit") != NULL);

    FILE *f = fopen(logpath, "w");
    fclose(f);
    mode("normal");
    game_reset(&g);
    e = fake(12, 0, 1500);
    o = opponent_uci(&e);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    opponent_free(o);
    slurp(logpath, buf, sizeof(buf));
    check("searches by depth", strstr(buf, "go depth 12") != NULL);
    check("no Elo options when the engine has no UCI_Elo", strstr(buf, "setoption") == NULL);

    unsetenv("FAKE_UCI_LOG");
    remove(logpath);
}

static void test_driver_stop_cancel(void)
{
    printf("== driver: stop and cancel ==\n");
    GameState g;
    game_reset(&g);
    EngineEntry e = fake(0, 60000, 0);
    SearchResult r;
    U64 key;

    mode("slow");
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    nap(300);
    long t0 = now_ms();
    opponent_stop(o);
    check("stop returns within 2s", now_ms() - t0 < 2000);
    check("and the next poll has its move", opponent_poll(o, &r, &key) && is_legal(&g, r.best_move));

    opponent_start(o, &g);
    nap(200);
    opponent_cancel(o);
    check("cancel leaves nothing to poll", !opponent_poll(o, &r, &key));
    check("and the driver starts again", opponent_start(o, &g));
    opponent_cancel(o);
    opponent_free(o);

    mode("deaf");
    o = opponent_uci(&e);
    opponent_start(o, &g);
    nap(300);
    t0 = now_ms();
    opponent_stop(o);
    long took = now_ms() - t0;
    check("an engine ignoring stop is killed after about 2s", took >= 1500 && took < 3000);
    check("and polls as no move", opponent_poll(o, &r, &key) && r.best_move == 0);
    check("with 'Fake: did not stop'",
          opponent_error(o) && strcmp(opponent_error(o), "Fake: did not stop") == 0);

    mode("normal");
    check("the next start relaunches it",
          opponent_start(o, &g) && wait_result(o, &r, &key, 3000) && is_legal(&g, r.best_move));
    check("and the error is cleared", opponent_error(o) == NULL);
    opponent_free(o);
    check("no child process is left", no_children());
}

static void expect_failure(const char *m, const char *path, const char *msg, long budget)
{
    GameState g;
    game_reset(&g);
    EngineEntry e = fake(0, 100, 0);
    snprintf(e.path, sizeof(e.path), "%s", path);
    SearchResult r;
    U64 key;
    char name[96];

    mode(m);
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    snprintf(name, sizeof(name), "%s: polls as no move", m);
    check(name, wait_result(o, &r, &key, budget) && r.best_move == 0);
    snprintf(name, sizeof(name), "%s: '%s'", m, msg);
    check(name, opponent_error(o) && strcmp(opponent_error(o), msg) == 0);
    opponent_free(o);
}

static void test_driver_failures(void)
{
    printf("== driver: failures ==\n");
    expect_failure("crash", "build/fake_uci", "Fake: engine exited", 3000);
    expect_failure("illegal", "build/fake_uci", "Fake: played illegal move e2e5", 3000);
    expect_failure("normal", "/nonexistent/dchess-engine",
                   "Fake: could not start /nonexistent/dchess-engine", 3000);
    long t0 = now_ms();
    expect_failure("mute", "build/fake_uci", "Fake: no reply from engine", 12000);
    check("mute waits out the 10s handshake", now_ms() - t0 >= 9000);
    t0 = now_ms();
    expect_failure("flood", "build/fake_uci", "Fake: no reply from engine", 15000);
    check("an engine flooding stdout still times out", now_ms() - t0 < 14000);
    check("no child process is left", no_children());
}

static void test_driver_edges(void)
{
    printf("== driver: edges ==\n");
    GameState g;
    SearchResult r;
    U64 key;
    EngineEntry e = fake(0, 100, 0);

    mode("normal");
    game_reset(&g);
    game_load_fen(&g, "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    check("a mated side gets no move and no error",
          wait_result(o, &r, &key, 3000) && r.best_move == 0 && opponent_error(o) == NULL);
    opponent_free(o);

    mode("mute");
    game_reset(&g);
    o = opponent_uci(&e);
    opponent_start(o, &g);
    long t0 = now_ms();
    opponent_cancel(o);
    check("cancel during the handshake returns at once", now_ms() - t0 < 500);
    check("and leaves nothing to poll", !opponent_poll(o, &r, &key));
    opponent_free(o);

    mode("chatty");
    game_reset(&g);
    o = opponent_uci(&e);
    opponent_start(o, &g);
    check("a 10 KB info line does not stop the search",
          wait_result(o, &r, &key, 3000) && is_legal(&g, r.best_move));
    opponent_free(o);
    check("no child process is left", no_children());
}

static void test_probe(void)
{
    printf("== probe ==\n");
    UciProbe p;
    char err[160];

    mode("normal");
    check("a probe reads the engine's name",
          uci_probe("build/fake_uci", &p, err, sizeof(err)) &&
          strcmp(p.name, "Fake UCI") == 0 && strcmp(p.author, "dchess tests") == 0);
    check("and sees no UCI_Elo", !p.elo_supported);
    mode("elo");
    check("and the Elo range when offered",
          uci_probe("build/fake_uci", &p, err, sizeof(err)) &&
          p.elo_supported && p.elo_min == 1320 && p.elo_max == 3190);
    check("a missing file is an error",
          !uci_probe("/nonexistent/dchess-engine", &p, err, sizeof(err)) &&
          strcmp(err, "could not start /nonexistent/dchess-engine") == 0);
    mode("mute");
    long t0 = now_ms();
    check("an engine that never answers times out",
          !uci_probe("build/fake_uci", &p, err, sizeof(err)) &&
          strcmp(err, "no reply from engine") == 0);
    check("after about 5s", now_ms() - t0 >= 4500 && now_ms() - t0 < 7000);
    check("no child process is left", no_children());
}

static void test_probe_not_an_engine(void)
{
    printf("== probe: not an engine ==\n");
    UciProbe p;
    char err[160];
    check("/bin/true exits: could not start",
          !uci_probe("/bin/true", &p, err, sizeof(err)) &&
          strcmp(err, "could not start /bin/true") == 0);
    mode("flood");
    long t0 = now_ms();
    check("an engine flooding stdout fails the probe",
          !uci_probe("build/fake_uci", &p, err, sizeof(err)) &&
          strcmp(err, "no reply from engine") == 0);
    check("within about 5s", now_ms() - t0 < 7000);
    check("/bin/cat echoes but never says uciok",
          !uci_probe("/bin/cat", &p, err, sizeof(err)) &&
          strcmp(err, "no reply from engine") == 0);
    check("no child process is left", no_children());
}

int main(void)
{
    init_attacks();

    test_info();
    test_bestmove();
    test_option();
    test_position();
    test_driver_normal();
    test_driver_log();
    test_driver_stop_cancel();
    test_driver_failures();
    test_driver_edges();
    test_probe();
    test_probe_not_an_engine();

    if (failures) {
        printf("\n%d UCI test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll UCI tests passed.\n");
    return 0;
}
