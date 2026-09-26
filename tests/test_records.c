/* The PGN game database.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "game/records.h"
#include "engine/move.h"
#include "utils/bitboard.h"
#include "utils/cli.h"

static int failures = 0;
static char dir[] = "/tmp/dchess-records-XXXXXX";
static char path[512];

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void fresh(const char *file)
{
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    remove(path);
}

static void play(GameState *g, const char *text)
{
    int from, to, promo;
    Move m;
    if (parse_move_str(text, &from, &to, &promo) && game_find_move(g, from, to, promo, &m))
        game_play(g, m);
}

static void finished(GameState *g, const char *result)
{
    game_reset(g);
    play(g, "e2e4");
    g->game_over = 1;
    snprintf(g->result, sizeof(g->result), "%s", result);
}

static Player prof(const char *name)
{
    Player p = player_human();
    snprintf(p.name, sizeof(p.name), "%s", name);
    return p;
}

static void test_round_trip(void)
{
    printf("== round trip ==\n");
    EngineList reg;
    memset(&reg, 0, sizeof(reg));
    reg.count = 1;
    snprintf(reg.e[0].name, sizeof(reg.e[0].name), "Fake");
    snprintf(reg.e[0].path, sizeof(reg.e[0].path), "x");
    reg.e[0].limit_ms = 1000;

    fresh("rt.pgn");
    GameState g;
    game_reset(&g);
    play(&g, "f2f3"); play(&g, "e7e5"); play(&g, "g2g4"); play(&g, "d8h4");
    game_update_status(&g);
    Player p[2] = { prof("saeed"), player_uci("Fake") };
    check("a finished game is appended", records_append(path, &g, p, &reg) == 1);

    RecordList l;
    check("and loads back", records_load(path, &l) == 1 && l.count == 1);
    Record *r = &l.r[0];
    check("names and kinds",
          strcmp(r->white, "saeed") == 0 && strcmp(r->black, "Fake") == 0 &&
          r->white_kind == KIND_PROFILE && r->black_kind == KIND_ENGINE);
    check("engine strength", strcmp(r->black_strength, "1s/move") == 0);
    check("result, reason and plies",
          r->result == -1 && strcmp(r->end_reason, "checkmate") == 0 && r->plies == 4);
    check("date and time", strlen(r->date) == 10 && strlen(r->time) == 8);
    records_free(&l);

    fresh("fen.pgn");
    game_reset(&g);
    game_load_fen(&g, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    play(&g, "a1a8");
    game_update_status(&g);
    Player q[2] = { prof("saeed"), player_builtin(DIFF_HARD) };
    records_append(path, &g, q, NULL);
    records_load(path, &l);
    check("a FEN start loads", l.count == 1 && l.r[0].result == 1 && l.r[0].plies == 1);
    check("with the dchess level as strength", strcmp(l.r[0].black_strength, "Hard") == 0);
    records_free(&l);
}

static void test_tally(void)
{
    printf("== tally ==\n");
    fresh("tally.pgn");
    GameState g;
    Player a[2] = { prof("saeed"), prof("alice") };
    finished(&g, "Checkmate — White wins!");
    records_append(path, &g, a, NULL);
    Player b[2] = { player_human(), prof("saeed") };
    finished(&g, "Draw by repetition!");
    records_append(path, &g, b, NULL);
    Player c[2] = { prof("saeed"), player_builtin(DIFF_EASY) };
    finished(&g, "White resigns — Black wins");
    records_append(path, &g, c, NULL);

    RecordList l;
    records_load(path, &l);
    RecordTally s = records_tally(&l, "saeed"), al = records_tally(&l, "alice");
    check("saeed: 3 games, 1 win, 1 draw, 1 loss",
          s.games == 3 && s.wins == 1 && s.draws == 1 && s.losses == 1);
    check("alice: 1 game, 1 loss", al.games == 1 && al.losses == 1);
    check("a guest counts for nobody", records_tally(&l, "Guest").games == 0);
    check("a lone guest is written as Guest", strcmp(l.r[1].white, "Guest") == 0 &&
          l.r[1].white_kind == KIND_GUEST);
    check("resigned is recorded", strcmp(l.r[2].end_reason, "resigned") == 0 && l.r[2].result == -1);
    records_free(&l);
}

static void test_legacy(void)
{
    printf("== legacy records ==\n");
    fresh("legacy.pgn");
    long t = 1700000000L;
    records_append_legacy(path, "saeed", t, 1);
    records_append_legacy(path, "saeed", t + 60, -1);
    records_append_legacy(path, "saeed", t + 120, 0);

    RecordList l;
    records_load(path, &l);
    float w[30];
    const Record *recent[8];
    check("three legacy records", l.count == 3 && l.r[0].legacy);
    check("they are not tallied", records_tally(&l, "saeed").games == 0);
    int n = records_winrate(&l, "saeed", w, 30);
    check("they feed the win rate",
          n == 3 && w[0] > 0.99f && w[1] > 0.49f && w[1] < 0.51f && w[2] > 0.33f && w[2] < 0.34f);
    check("and show as recent, newest first",
          records_recent(&l, "saeed", recent, 8) == 3 && recent[0]->result == 0);
    records_free(&l);
}

static void test_rename_spaces(void)
{
    printf("== rename ==\n");
    fresh("rename.pgn");
    GameState g;
    Player a[2] = { prof("saeed"), player_uci("saeed") };
    finished(&g, "Checkmate — White wins!");
    records_append(path, &g, a, NULL);
    Player b[2] = { prof("alice"), prof("saeed") };
    finished(&g, "Checkmate — Black wins!");
    records_append(path, &g, b, NULL);

    check("rename succeeds", records_rename(path, "saeed", "Sa Eed") == 1);
    RecordList l;
    records_load(path, &l);
    check("the profile side is relabelled", strcmp(l.r[0].white, "Sa Eed") == 0 &&
          strcmp(l.r[1].black, "Sa Eed") == 0);
    check("an engine with the same name is not", strcmp(l.r[0].black, "saeed") == 0);
    check("the history follows the new name",
          records_tally(&l, "Sa Eed").games == 2 && records_tally(&l, "saeed").games == 0);
    records_free(&l);
}

static void test_damaged(void)
{
    printf("== damaged files ==\n");
    RecordList l;
    check("a missing file loads nothing", records_load("/nonexistent/games.pgn", &l) == 0 && l.count == 0);

    fresh("empty.pgn");
    fclose(fopen(path, "w"));
    check("an empty file loads nothing", records_load(path, &l) == 1 && l.count == 0);

    fresh("damaged.pgn");
    GameState g;
    Player p[2] = { prof("saeed"), prof("alice") };
    finished(&g, "Checkmate — White wins!");
    records_append(path, &g, p, NULL);
    FILE *f = fopen(path, "a");
    fputs("garbage\n[White \"x\"\n", f);
    fclose(f);
    records_append(path, &g, p, NULL);
    check("stray text between games is skipped", records_load(path, &l) == 1 && l.count == 2);
    records_free(&l);
}

static void test_speed(void)
{
    printf("== speed ==\n");
    fresh("big.pgn");
    GameState g;
    Player p[2] = { prof("saeed"), player_builtin(DIFF_MEDIUM) };
    finished(&g, "Checkmate — White wins!");
    for (int i = 0; i < 5000; i++) records_append(path, &g, p, NULL);

    struct timespec a, b;
    RecordList l;
    clock_gettime(CLOCK_MONOTONIC, &a);
    records_load(path, &l);
    clock_gettime(CLOCK_MONOTONIC, &b);
    long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
    check("5000 games load", l.count == 5000);
    check("in under 500 ms", ms < 500);
    if (ms >= 500) printf("        took %ld ms\n", ms);
    records_free(&l);
}

static void test_to_stats(void)
{
    printf("== stats view ==\n");
    fresh("stats.pgn");
    GameState g;
    Player a[2] = { prof("saeed"), player_builtin(DIFF_HARD) };
    finished(&g, "Checkmate — White wins!");
    records_append(path, &g, a, NULL);
    Player b[2] = { player_builtin(DIFF_EASY), prof("saeed") };
    finished(&g, "Checkmate — White wins!");
    records_append(path, &g, b, NULL);
    Player c[2] = { prof("saeed"), prof("alice") };
    finished(&g, "Draw by repetition!");
    records_append(path, &g, c, NULL);

    RecordList l;
    DchessStats s;
    records_load(path, &l);
    records_to_stats(&l, "saeed", &s);
    check("a win against dchess Hard", s.games_played[2] == 1 && s.wins[2] == 1);
    check("a loss against dchess Easy", s.games_played[0] == 1 && s.losses[0] == 1);
    check("the colour split counts every game", s.played_as_white == 2 && s.played_as_black == 1);
    check("the history has all three", s.history_count == 3 && s.history[0].result == 1);
    records_free(&l);
}


static void test_rename_long_lines(void)
{
    printf("== rename keeps long lines ==\n");
    fresh("long.pgn");
    FILE *f = fopen(path, "w");
    fputs("[Event \"x\"]\n[White \"saeed\"]\n[Black \"bob\"]\n[Result \"1-0\"]\n"
          "[WhiteKind \"profile\"]\n[BlackKind \"guest\"]\n\n", f);
    for (int i = 1; i <= 400; i++) fprintf(f, "%d. Nf3 Nf6 ", i);
    fputs("1-0\n", f);
    fclose(f);

    records_rename(path, "saeed", "neo");
    static char buf[16384];
    f = fopen(path, "r");
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    int lines = 0;
    for (char *p = buf; *p; p++) lines += *p == '\n';
    check("the name is relabelled", strstr(buf, "[White \"neo\"]") != NULL);
    check("a long movetext line is copied whole", lines == 8 && strstr(buf, "400. Nf3 Nf6 1-0\n"));
}


static void test_damaged_event(void)
{
    printf("== a damaged Event line ==\n");
    fresh("event.pgn");
    FILE *f = fopen(path, "w");
    fputs("[Event \"a\"]\n[White \"saeed\"]\n[Black \"alice\"]\n[Result \"1-0\"]\n"
          "[WhiteKind \"profile\"]\n[BlackKind \"profile\"]\n\n1-0\n\n"
          "[Event \"b\"\n[White \"bob\"]\n[Black \"carol\"]\n[Result \"0-1\"]\n"
          "[WhiteKind \"profile\"]\n[BlackKind \"profile\"]\n\n0-1\n", f);
    fclose(f);
    RecordList l;
    records_load(path, &l);
    check("both games load", l.count == 2);
    check("neither overwrites the other",
          l.count == 2 && strcmp(l.r[0].white, "saeed") == 0 && strcmp(l.r[1].white, "bob") == 0 &&
          l.r[0].result == 1 && l.r[1].result == -1);
    records_free(&l);
}


static void test_unicode_record(void)
{
    printf("== unicode names in records ==\n");
    fresh("uni.pgn");
    const char *ru24 = "ЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖЖ";
    GameState g;
    Player p[2] = { player_profile(ru24), player_builtin(DIFF_EASY) };
    finished(&g, "Checkmate — White wins!");
    records_append(path, &g, p, NULL);
    RecordList l;
    records_load(path, &l);
    check("a 48-byte name is stored whole", l.count == 1 && strcmp(l.r[0].white, ru24) == 0);
    check("and tallies", records_tally(&l, ru24).wins == 1);
    records_free(&l);
}

int main(void)
{
    init_attacks();
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }

    test_round_trip();
    test_tally();
    test_legacy();
    test_rename_spaces();
    test_rename_long_lines();
    test_damaged();
    test_damaged_event();
    test_unicode_record();
    test_speed();
    test_to_stats();

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) printf("  (could not clean %s)\n", dir);

    if (failures) {
        printf("\n%d records test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll records tests passed.\n");
    return 0;
}
