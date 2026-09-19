/* PGN export tests.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "game/pgn.h"
#include "game/game.h"
#include "engine/movegen.h"
#include "engine/move.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include "test_common.h"

static int failures = 0;
static char out[8192];

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

/* Write the game to a scratch file and slurp it back. */
static int render(const GameState *g, const PgnHeader *h)
{
    const char *path = "/tmp/dchess-test.pgn";
    if (pgn_write(g, h, path) != 0) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(out, 1, sizeof(out) - 1, f);
    out[n] = '\0';
    fclose(f);
    remove(path);
    return 1;
}

static int has(const char *needle) { return strstr(out, needle) != NULL; }

static void test_tags_and_movetext(void)
{
    printf("== tags and movetext ==\n");

    GameState g;
    game_reset(&g);
    const char *moves[] = { "e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6" };
    for (int i = 0; i < 6; i++) play(&g, moves[i]);

    PgnHeader h = { "Test Event", "nowhere", "Alice", "Bob" };
    check("the file is written", render(&g, &h) == 1);

    check("event tag", has("[Event \"Test Event\"]"));
    check("site tag", has("[Site \"nowhere\"]"));
    check("player tags", has("[White \"Alice\"]") && has("[Black \"Bob\"]"));
    check("date tag is present", has("[Date \""));
    check("a blank line separates tags from movetext",
          strstr(out, "]\n\n") != NULL);

    check("movetext is SAN with move numbers",
          has("1. e4 e5 2. Nf3 Nc6 3. Bb5 a6"));
    check("no SetUp tag for a standard game", !has("[SetUp"));
    check("NULL header fields fall back to defaults",
          render(&g, NULL) && has("[White \"White\"]"));
}

static void test_result_tokens(void)
{
    printf("== result tokens ==\n");

    /* Unfinished. */
    GameState g;
    game_reset(&g);
    play(&g, "e2e4");
    render(&g, NULL);
    check("an unfinished game is \"*\"", has("[Result \"*\"]") && has(" *\n"));

    /* Fool's mate: Black delivers it, so Black wins. */
    GameState m;
    game_reset(&m);
    play(&m, "f2f3"); play(&m, "e7e5"); play(&m, "g2g4"); play(&m, "d8h4");
    game_update_status(&m);
    render(&m, NULL);
    check("a win for Black is \"0-1\"", has("[Result \"0-1\"]"));
    check("and the movetext ends with it", has("Qh4# 0-1"));

    /* Stalemate is a draw. */
    GameState d;
    game_reset(&d);
    setup_position(&d.pos,
        "........" "........" "........" "........"
        "........" "......Q." ".....K.." ".......k",
        BLACK, 0, -1);
    d.game_over = 0; d.result[0] = '\0'; d.position_count = 0;
    game_update_status(&d);
    render(&d, NULL);
    check("a draw is \"1/2-1/2\"", has("[Result \"1/2-1/2\"]"));
}

static void test_setup_position(void)
{
    printf("== games from a position ==\n");

    GameState g;
    game_reset(&g);
    const char *fen = "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1";
    check("the FEN loads", game_load_fen(&g, fen) == 1);
    play(&g, "e2e4");

    render(&g, NULL);
    check("SetUp tag is emitted", has("[SetUp \"1\"]"));
    check("FEN tag carries the starting position", has(fen));
    check("movetext still starts at move 1", has("1. e4"));

    /* A new game must forget it again. */
    game_reset(&g);
    play(&g, "d2d4");
    render(&g, NULL);
    check("a fresh game drops both tags", !has("[SetUp") && !has("[FEN"));
}

static void test_line_wrapping(void)
{
    printf("== line wrapping ==\n");

    GameState g;
    game_reset(&g);
    /* Shuffle knights back and forth to pile up tokens cheaply. */
    const char *cycle[] = { "g1f3", "g8f6", "f3g1", "f6g8" };
    for (int i = 0; i < 24; i++) play(&g, cycle[i % 4]);

    render(&g, NULL);

    const char *movetext = strstr(out, "]\n\n");
    check("movetext found", movetext != NULL);
    int longest = 0, run = 0, ok = 1;
    for (const char *p = movetext ? movetext + 3 : out; *p; p++) {
        if (*p == '\n') { if (run > longest) longest = run; run = 0; }
        else run++;
    }
    if (run > longest) longest = run;
    if (longest > 80) ok = 0;
    check("no movetext line exceeds 80 columns", ok);
    if (!ok) printf("        longest line was %d\n", longest);
    check("it actually wrapped", longest > 0 && strchr(movetext + 3, '\n') != NULL);
}

int main(void)
{
    init_attacks();

    test_tags_and_movetext();
    test_result_tokens();
    test_setup_position();
    test_line_wrapping();

    if (failures) {
        printf("\n%d PGN test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll PGN tests passed.\n");
    return 0;
}
