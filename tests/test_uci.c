/* UCI protocol: parsers, position command, and (Task 4) the driver.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/uci.h"
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

int main(void)
{
    init_attacks();

    test_info();
    test_bestmove();
    test_option();
    test_position();

    if (failures) {
        printf("\n%d UCI test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll UCI tests passed.\n");
    return 0;
}
