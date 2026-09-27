/* Reading games back from PGN files.
 *
 * Build & run:  make test
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "game/replay.h"
#include "game/pgn.h"
#include "engine/move.h"
#include "utils/bitboard.h"

static int failures = 0;
static char dir[] = "/tmp/dchess_replay_XXXXXX";
static char path[512];

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void file(const char *name, const char *text)
{
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    fputs(text, f);
    fclose(f);
}

static void play(GameState *g, const char *text)
{
    int from, to, promo;
    Move m;
    if (parse_move_str(text, &from, &to, &promo) && game_find_move(g, from, to, promo, &m))
        game_play(g, m);
}

/* The game's moves as SAN, space separated. */
static const char *sans(const ReplayGame *r)
{
    static char out[8192];
    static GameState g;
    game_reset(&g);
    if (r->fen[0]) game_load_fen(&g, r->fen);
    out[0] = '\0';
    for (int i = 0; i < r->count; i++) {
        game_play(&g, r->moves[i]);
        if (i) strcat(out, " ");
        strcat(out, g.move_history[g.move_count - 1]);
    }
    return out;
}

static ReplayGame rg;
static char err[128];

static int read_first(const char *text)
{
    file("t.pgn", text);
    return replay_read(path, 0, &rg, err, sizeof(err));
}

static void test_round_trip(void)
{
    printf("== round trip ==\n");
    static GameState g;
    game_reset(&g);
    const char *line[] = { "e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "e1g1" };
    for (int i = 0; i < 7; i++) play(&g, line[i]);
    snprintf(path, sizeof(path), "%s/rt.pgn", dir);
    PgnHeader h = { .event = "Casual game", .site = "dchess", .white = "a", .black = "b" };
    pgn_write(&g, &h, path);
    check("a written game reads back", replay_read(path, 0, &rg, err, sizeof(err)) == 1 && !rg.err[0]);
    check("with the same moves", !strcmp(sans(&rg), "e4 e5 Nf3 Nc6 Bb5 a6 O-O"));
}

static void test_skipping(void)
{
    printf("== comments and variations ==\n");
    read_first("[Event \"x\"]\n\n1. e4 {best by test} 1... e5 (1... d5 (1... c5 2. Nf3)) 2. Nf3 $1 ; line comment\n"
               "Nc6\n1-0\n");
    check("comments, variations and NAGs are skipped", !strcmp(sans(&rg), "e4 e5 Nf3 Nc6") && !rg.err[0]);
    read_first("[Event \"x\"]\n\n1.e4 e5 2.Nf3 Nc6 *\n");
    check("move numbers without a space", !strcmp(sans(&rg), "e4 e5 Nf3 Nc6") && !rg.err[0]);
}

static void test_forms(void)
{
    printf("== move forms ==\n");
    read_first("[Event \"x\"]\n\n1. e4 e5 2. Nf3 Nc6 3. Bc4 Nf6 4. 0-0 Be7 5. d3 o-o 1/2-1/2\n");
    check("0-0 and o-o castle", !strcmp(sans(&rg), "e4 e5 Nf3 Nc6 Bc4 Nf6 O-O Be7 d3 O-O") && !rg.err[0]);
    read_first("[Event \"x\"]\n[SetUp \"1\"]\n[FEN \"4k3/P7/8/8/8/8/8/4K3 w - - 0 1\"]\n\n1. a8=Q+ Kd7 *\n");
    check("a FEN game starts from its position", !strcmp(rg.fen, "4k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
    check("a promotion with check", !strcmp(sans(&rg), "a8=Q+ Kd7") && !rg.err[0]);
    read_first("[Event \"x\"]\n\n1. Nf3 d5 2. d4 Nf6 3. Nbd2 Nbd7 4. e4 dxe4 5. Nxe4 Nxe4 *\n");
    check("disambiguated knights", !strcmp(sans(&rg), "Nf3 d5 d4 Nf6 Nbd2 Nbd7 e4 dxe4 Nxe4 Nxe4") && !rg.err[0]);
    read_first("[Event \"x\"]\n\n1. e4 d5 2. e5 f5 3. exf6 e.p. e6 4. Qh5+ g6 *\n");
    check("e.p. and check marks", !strcmp(sans(&rg), "e4 d5 e5 f5 exf6 e6 Qh5+ g6") && !rg.err[0]);
}

static void test_bad_move(void)
{
    printf("== a move that cannot be read ==\n");
    read_first("[Event \"x\"]\n\n1. e4 e5 2. Qxh9 Nc6 *\n");
    check("the moves before it are kept", rg.count == 2);
    check("and it says where", strstr(rg.err, "stopped at move 2") && strstr(rg.err, "Qxh9"));
}

static void test_list(void)
{
    printf("== listing a file ==\n");
    file("three.pgn",
         "\xEF\xBB\xBF[Event \"one\"]\r\n[White \"Big \\\"Al\\\"\"]\r\n[Black \"b\"]\r\n[Result \"1-0\"]\r\n\r\n1. e4 1-0\r\n\r\n"
         "some junk here\n\n"
         "[Event \"two\"]\n[White \"c\"]\n[Black \"d\"]\n[Result \"*\"]\n\n*\n\n"
         "[Event \"three\"]\n[White \"e\"]\n[Black \"f\"]\n[Result \"0-1\"]\n[Date \"2026.09.27\"]\n\n1. f3 e5 2. g4 Qh4# 0-1");
    ReplayList l;
    check("three games", replay_list(path, &l, err, sizeof(err)) == 1 && l.count == 3);
    if (l.count != 3) return;
    check("names are unescaped", !strcmp(l.e[0].white, "Big \"Al\"") && !strcmp(l.e[0].result, "1-0"));
    check("tags are read", !strcmp(l.e[2].white, "e") && !strcmp(l.e[2].date, "2026.09.27") &&
                           !strcmp(l.e[1].event, "two"));
    check("CRLF and a BOM game reads", replay_read(path, l.e[0].offset, &rg, err, sizeof(err)) &&
                                       !strcmp(sans(&rg), "e4") && !rg.err[0]);
    check("a game with no moves reads as empty", replay_read(path, l.e[1].offset, &rg, err, sizeof(err)) &&
                                                 rg.count == 0 && !rg.err[0]);
    check("the last game, with no final newline", replay_read(path, l.e[2].offset, &rg, err, sizeof(err)) &&
                                                   !strcmp(sans(&rg), "f3 e5 g4 Qh4#") && !rg.err[0]);
    replay_list_free(&l);
}

static void test_limits(void)
{
    printf("== limits ==\n");
    snprintf(path, sizeof(path), "%s/big.pgn", dir);
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    int ok = fd >= 0 && ftruncate(fd, 65L * 1024 * 1024) == 0;
    if (fd >= 0) close(fd);
    ReplayList l;
    check("a 65 MiB file is refused", ok && replay_list(path, &l, err, sizeof(err)) == 0 && err[0]);
    check("so is a missing one", replay_list("/nonexistent.pgn", &l, err, sizeof(err)) == 0);

    static char text[16384];
    strcpy(text, "[Event \"x\"]\n\n");
    for (int i = 0; i < 280; i++) strcat(text, "Nf3 Nf6 Ng1 Ng8 ");
    strcat(text, "*\n");
    read_first(text);
    check("a very long game stops at the limit", rg.count == MAX_MOVE_HISTORY);
}

int main(void)
{
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    init_attacks();

    test_round_trip();
    test_skipping();
    test_forms();
    test_bad_move();
    test_list();
    test_limits();

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) printf("  (could not clean %s)\n", dir);

    if (failures) {
        printf("\n%d replay test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll replay tests passed.\n");
    return 0;
}
