/* Opening books: Polyglot keys, file and built-in books, opening names.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/book.h"
#include "game/openings.h"
#include "utils/cli.h"
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "engine/move.h"
#include "utils/bitboard.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void play_line(GameState *g, const char *moves)
{
    char buf[256], *save;
    snprintf(buf, sizeof(buf), "%s", moves);
    for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
        int from, to, promo;
        Move m;
        if (parse_move_str(t, &from, &to, &promo) && game_find_move(g, from, to, promo, &m))
            game_play(g, m);
    }
}

static U64 key_after(const char *moves)
{
    GameState g;
    game_reset(&g);
    play_line(&g, moves);
    return book_key(&g.pos);
}

static void test_keys(void)
{
    printf("== Polyglot keys ==\n");
    check("start position", key_after("") == 0x463b96181691fc9cULL);
    check("e2e4", key_after("e2e4") == 0x823c9b50fd114196ULL);
    check("e2e4 d7d5", key_after("e2e4 d7d5") == 0x0756b94461c50fb0ULL);
    check("e2e4 d7d5 e4e5", key_after("e2e4 d7d5 e4e5") == 0x662fafb965db29d4ULL);
    check("e2e4 d7d5 e4e5 f7f5 (en passant possible)",
          key_after("e2e4 d7d5 e4e5 f7f5") == 0x22a48b5a8e47ff78ULL);
    check("... e1e2 (castling rights lost)",
          key_after("e2e4 d7d5 e4e5 f7f5 e1e2") == 0x652a607ca3f242c1ULL);
    check("... e8f7", key_after("e2e4 d7d5 e4e5 f7f5 e1e2 e8f7") == 0x00fdd303c946bdd9ULL);
    check("a2a4 b7b5 h2h4 b5b4 c2c4 (ep square, capture possible)",
          key_after("a2a4 b7b5 h2h4 b5b4 c2c4") == 0x3c8123ea7b067637ULL);
    check("... b4c3 a1a3", key_after("a2a4 b7b5 h2h4 b5b4 c2c4 b4c3 a1a3") == 0x5c3f9b829b279560ULL);

    GameState a, b;
    game_reset(&a);
    game_load_fen(&a, "4k3/8/8/8/3P4/8/8/4K3 b - d3 0 1");
    game_reset(&b);
    game_load_fen(&b, "4k3/8/8/8/3P4/8/8/4K3 b - - 0 1");
    check("an ep square nobody can capture onto is ignored", book_key(&a.pos) == book_key(&b.pos));
}


typedef struct { U64 key; unsigned short move, weight; } Raw;

static unsigned short pg_move(int from, int to, int promo)
{
    return (unsigned short)((to % 8) | (to / 8) << 3 | (from % 8) << 6 | (from / 8) << 9 | promo << 12);
}

static int by_key(const void *a, const void *b)
{
    U64 x = ((const Raw *)a)->key, y = ((const Raw *)b)->key;
    return x < y ? -1 : x > y;
}

static void write_bin(const char *path, Raw *r, int n)
{
    qsort(r, (size_t)n, sizeof(Raw), by_key);
    FILE *f = fopen(path, "wb");
    for (int i = 0; i < n; i++) {
        unsigned char e[16] = { 0 };
        for (int b = 0; b < 8; b++) e[b] = (unsigned char)(r[i].key >> (56 - 8 * b));
        e[8] = (unsigned char)(r[i].move >> 8); e[9] = (unsigned char)r[i].move;
        e[10] = (unsigned char)(r[i].weight >> 8); e[11] = (unsigned char)r[i].weight;
        fwrite(e, 1, 16, f);
    }
    fclose(f);
}

static void test_file_book(void)
{
    printf("== Polyglot file ==\n");
    const char *path = "/tmp/dchess-test-book.bin";
    char err[128];
    GameState g, c, pr;
    game_reset(&g);
    game_reset(&c);
    play_line(&c, "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6");
    game_reset(&pr);
    game_load_fen(&pr, "8/P6k/8/8/8/8/8/K7 w - - 0 1");

    Raw r[] = {
        { book_key(&g.pos), pg_move(e2, e4, 0), 3 },
        { book_key(&g.pos), pg_move(d2, d4, 0), 1 },
        { book_key(&g.pos), pg_move(e2, e5, 0), 5 },            /* illegal */
        { book_key(&g.pos), pg_move(g1, f3, 0), 0 },            /* weight 0: never */
        { book_key(&c.pos), pg_move(e1, h1, 0), 1 },            /* castling, king takes rook */
        { book_key(&pr.pos), pg_move(a7, a8, 4), 1 },           /* promotion to a queen */
    };
    write_bin(path, r, 6);
    Book *b = book_open(path, err, sizeof(err));
    check("a .bin opens", b != NULL);

    unsigned rng = 12345;
    int n_e4 = 0, n_d4 = 0, other = 0;
    for (int i = 0; i < 400; i++) {
        Move m = book_pick(b, &g, DIFF_HARD, &rng);
        if (FROM(m) == e2 && TO(m) == e4) n_e4++;
        else if (FROM(m) == d2 && TO(m) == d4) n_d4++;
        else other++;
    }
    check("only legal, weighted moves are picked (never weight 0)", other == 0);
    check("weights are respected (about 3:1)", n_e4 > 240 && n_e4 < 360 && n_d4 > 40);
    Move castle = book_pick(b, &c, DIFF_HARD, &rng);
    check("king-takes-rook becomes castling", FROM(castle) == e1 && TO(castle) == g1);
    Move promo = book_pick(b, &pr, DIFF_HARD, &rng);
    check("a promotion decodes", FROM(promo) == a7 && TO(promo) == a8 && (FLAGS(promo) & FLAG_PROMO_Q));
    GameState off;
    game_reset(&off);
    play_line(&off, "h2h4");
    check("out of book is 0", book_pick(b, &off, DIFF_HARD, &rng) == 0);
    book_free(b);

    check("a missing file is refused", !book_open("/nonexistent/x.bin", err, sizeof(err)) && err[0]);
    FILE *f = fopen(path, "wb");
    fputs("12345678901234567", f);
    fclose(f);
    err[0] = '\0';
    check("a size that is not a multiple of 16 is refused", !book_open(path, err, sizeof(err)) && err[0]);
    int fd = open(path, O_WRONLY | O_TRUNC);
    check("a huge file is refused without reading it",
          fd >= 0 && ftruncate(fd, 600L * 1024 * 1024) == 0 && close(fd) == 0 &&
          !book_open(path, err, sizeof(err)));
    remove(path);

    err[0] = '\0';
    check("a directory is refused", !book_open("tests", err, sizeof(err)) && err[0]);
    fclose(fopen(path, "wb"));
    err[0] = '\0';
    check("an empty file is refused", !book_open(path, err, sizeof(err)) && err[0]);
    remove(path);
}

static void test_builtin(void)
{
    printf("== built-in book ==\n");
    int bad = 0, unnamed = 0;
    for (int i = 0; i < OPENINGS_COUNT; i++) {
        GameState g;
        game_reset(&g);
        char buf[256], *save;
        int ok = 1;
        snprintf(buf, sizeof(buf), "%s", OPENINGS[i].moves);
        for (char *t = strtok_r(buf, " ", &save); t && ok; t = strtok_r(NULL, " ", &save)) {
            int from, to, promo;
            Move m;
            ok = parse_move_str(t, &from, &to, &promo) && game_find_move(&g, from, to, promo, &m);
            if (ok) game_play(&g, m);
        }
        const char *eco = NULL, *name = book_opening(&g, &eco);
        if (!ok) { bad++; printf("        illegal: %s %s\n", OPENINGS[i].eco, OPENINGS[i].name); }
        else if (!name || strcmp(name, OPENINGS[i].name) != 0 || strcmp(eco, OPENINGS[i].eco) != 0) {
            unnamed++;
            printf("        misnamed: %s %s -> %s\n", OPENINGS[i].eco, OPENINGS[i].name, name ? name : "(none)");
        }
    }
    check("at least 100 lines", OPENINGS_COUNT >= 100);
    check("every line is legal", bad == 0);
    check("every line names itself", unnamed == 0);

    const char *eco;
    GameState a, b;
    game_reset(&a); play_line(&a, "g1f3 d7d5 d2d4");
    game_reset(&b); play_line(&b, "d2d4 d7d5 g1f3");
    const char *na = book_opening(&a, &eco), *nb = book_opening(&b, &eco);
    check("a transposition names the same opening", na && nb && strcmp(na, nb) == 0);

    game_reset(&a);
    play_line(&a, "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6 h2h3");
    na = book_opening(&a, &eco);
    check("leaving the book keeps the last name", na && strstr(na, "Najdorf") && strcmp(eco, "B90") == 0);

    game_reset(&a);
    game_load_fen(&a, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    check("a FEN game has no name", book_opening(&a, &eco) == NULL);

    Book *bk = book_builtin();
    unsigned rng = 7;
    check("the built-in book opens", bk != NULL);
    GameState s;
    game_reset(&s);
    check("it has a move for the start position", book_pick(bk, &s, DIFF_HARD, &rng) != 0);

    /* Ruy Lopez Chigorin runs 21 plies. */
    const char *ruy = "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 c2c3";
    game_reset(&s); play_line(&s, "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6");
    check("Easy stops at 8 plies", book_pick(bk, &s, DIFF_EASY, &rng) == 0 && book_pick(bk, &s, DIFF_HARD, &rng) != 0);
    game_reset(&s); play_line(&s, ruy);
    check("Medium still plays at 15 plies", book_pick(bk, &s, DIFF_MEDIUM, &rng) != 0);
    play_line(&s, "e8g8");
    check("and stops at 16", book_pick(bk, &s, DIFF_MEDIUM, &rng) == 0 && book_pick(bk, &s, DIFF_HARD, &rng) != 0);
    book_free(bk);
}

int main(void)
{
    init_attacks();
    test_keys();
    test_file_book();
    test_builtin();

    if (failures) {
        printf("\n%d book test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll book tests passed.\n");
    return 0;
}
