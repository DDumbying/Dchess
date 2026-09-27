/* Opening books: Polyglot keys, file and built-in books, opening names.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/book.h"
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

int main(void)
{
    init_attacks();
    test_keys();

    if (failures) {
        printf("\n%d book test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll book tests passed.\n");
    return 0;
}
