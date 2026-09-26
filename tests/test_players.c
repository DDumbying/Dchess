/* Player rules.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/players.h"
#include "utils/cli.h"
#include "utils/constants.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static Player H(void)       { return player_human(); }
static Player E(int level)  { return player_builtin(level); }

static void test_constructors(void)
{
    printf("== constructors ==\n");
    check("a human is PLAYER_HUMAN", H().kind == PLAYER_HUMAN);
    Player e = E(DIFF_HARD);
    check("a built-in keeps its level",
          e.kind == PLAYER_BUILTIN && e.level == DIFF_HARD);
    check("and takes that level's depth and time",
          e.depth == cli_depth_for_difficulty(DIFF_HARD) &&
          e.time_ms == cli_time_limit_for_difficulty(DIFF_HARD));
    check("an out-of-range level falls back to medium", E(7).level == DIFF_MEDIUM);
    check("level names parse",
          players_level_from_name("easy") == DIFF_EASY &&
          players_level_from_name("medium") == DIFF_MEDIUM &&
          players_level_from_name("hard") == DIFF_HARD);
    check("an unknown level name is -1", players_level_from_name("expert") == -1);
    check("level display names",
          strcmp(players_level_name(DIFF_EASY), "Easy") == 0 &&
          strcmp(players_level_name(DIFF_HARD), "Hard") == 0);
}

static void test_automated(void)
{
    printf("== automated ==\n");
    Player p[2] = { H(), E(DIFF_EASY) };
    check("a human side is not automated", !players_automated(p, WHITE));
    check("an engine side is", players_automated(p, BLACK));
}

static void test_undo_plies(void)
{
    printf("== undo ==\n");
    Player he[2] = { H(), E(DIFF_MEDIUM) };
    check("vs engine, human to move: 2 plies", players_undo_plies(he, WHITE, 10) == 2);
    check("vs engine, engine to move: 1 ply", players_undo_plies(he, BLACK, 10) == 1);
    check("vs engine, human to move, 1 ply of history: 0",
          players_undo_plies(he, WHITE, 1) == 0);

    Player eh[2] = { E(DIFF_MEDIUM), H() };
    check("engine as White, human to move: 2", players_undo_plies(eh, BLACK, 10) == 2);
    check("engine as White, engine to move: 1", players_undo_plies(eh, WHITE, 10) == 1);

    Player hh[2] = { H(), H() };
    check("two humans: 1 ply for either side",
          players_undo_plies(hh, WHITE, 5) == 1 && players_undo_plies(hh, BLACK, 5) == 1);

    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    check("two engines: 1 ply for either side",
          players_undo_plies(ee, WHITE, 5) == 1 && players_undo_plies(ee, BLACK, 5) == 1);

    check("no history: 0 in every pairing",
          players_undo_plies(he, BLACK, 0) == 0 &&
          players_undo_plies(hh, WHITE, 0) == 0 &&
          players_undo_plies(ee, WHITE, 0) == 0);
}

static void test_should_start(void)
{
    printf("== when an engine starts ==\n");
    Player he[2] = { H(), E(DIFF_MEDIUM) };
    check("an engine to move starts", players_should_start(he, BLACK, 0, 0, 100000));
    check("against a human there is no delay", players_should_start(he, BLACK, 0, 0, 0));
    check("a human to move never starts", !players_should_start(he, WHITE, 0, 0, 100000));
    check("not while paused", !players_should_start(he, BLACK, 1, 0, 100000));
    check("not once the game is over", !players_should_start(he, BLACK, 0, 1, 100000));

    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    check("two engines wait out the delay",
          !players_should_start(ee, WHITE, 0, 0, PLAYERS_AUTOPLAY_DELAY_MS - 1));
    check("and start once it has passed",
          players_should_start(ee, WHITE, 0, 0, PLAYERS_AUTOPLAY_DELAY_MS));

    Player hh[2] = { H(), H() };
    check("two humans: never",
          !players_should_start(hh, WHITE, 0, 0, 100000) &&
          !players_should_start(hh, BLACK, 0, 0, 100000));
}

static void test_stats_entry(void)
{
    printf("== which games are recorded ==\n");
    int side = -1, level = -1;

    Player he[2] = { H(), E(DIFF_HARD) };
    check("human vs engine is recorded", players_stats_entry(he, &side, &level) == 1);
    check("with the human's colour and the engine's level",
          side == WHITE && level == DIFF_HARD);

    Player eh[2] = { E(DIFF_EASY), H() };
    check("and with the human as Black",
          players_stats_entry(eh, &side, &level) == 1 && side == BLACK && level == DIFF_EASY);

    Player hh[2] = { H(), H() };
    check("two humans are not recorded", players_stats_entry(hh, &side, &level) == 0);
    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    check("two engines are not recorded", players_stats_entry(ee, &side, &level) == 0);
}

static void test_commands(void)
{
    printf("== player commands ==\n");
    char err[128];
    Player p[2] = { H(), E(DIFF_MEDIUM) };

    check("'black human' applies",
          players_apply_command(p, "black human", err, sizeof(err)) == 1 &&
          p[BLACK].kind == PLAYER_HUMAN);
    check("'white engine hard' applies",
          players_apply_command(p, "white engine hard", err, sizeof(err)) == 1 &&
          p[WHITE].kind == PLAYER_BUILTIN && p[WHITE].level == DIFF_HARD);
    check("'black engine' defaults to medium",
          players_apply_command(p, "black engine", err, sizeof(err)) == 1 &&
          p[BLACK].kind == PLAYER_BUILTIN && p[BLACK].level == DIFF_MEDIUM);

    players_apply_command(p, "white human", err, sizeof(err));
    check("'swap' exchanges the players",
          players_apply_command(p, "swap", err, sizeof(err)) == 1 &&
          p[WHITE].kind == PLAYER_BUILTIN && p[BLACK].kind == PLAYER_HUMAN);

    Player keep[2] = { p[WHITE], p[BLACK] };
    check("an unknown level is refused",
          players_apply_command(p, "white engine expert", err, sizeof(err)) == -1 &&
          strstr(err, "expert") != NULL);
    check("a level for a human is refused",
          players_apply_command(p, "black human hard", err, sizeof(err)) == -1);
    check("a bare colour is refused",
          players_apply_command(p, "white", err, sizeof(err)) == -1);
    check("an unknown role is refused",
          players_apply_command(p, "white robot", err, sizeof(err)) == -1);
    check("refused commands change nothing", memcmp(keep, p, sizeof(keep)) == 0);

    check("an unknown colour is not a player command",
          players_apply_command(p, "green human", err, sizeof(err)) == 0);
    check("a move is not a player command",
          players_apply_command(p, "b1c3", err, sizeof(err)) == 0);
    check("nor is 'new'", players_apply_command(p, "new", err, sizeof(err)) == 0);
}

static void test_labels(void)
{
    printf("== labels ==\n");
    char buf[64];
    Player h = H(), e = E(DIFF_HARD);

    player_label(&h, buf, sizeof(buf));
    check("a human is 'You'", strcmp(buf, "You") == 0);
    player_label(&e, buf, sizeof(buf));
    check("an engine is 'dchess Hard'", strcmp(buf, "dchess Hard") == 0);

    Player he[2] = { H(), E(DIFF_HARD) };
    players_pgn_name(he, WHITE, buf, sizeof(buf));
    check("PGN: a lone human is 'Player'", strcmp(buf, "Player") == 0);
    players_pgn_name(he, BLACK, buf, sizeof(buf));
    check("PGN: the engine is 'dchess (Hard)'", strcmp(buf, "dchess (Hard)") == 0);

    Player hh[2] = { H(), H() };
    players_pgn_name(hh, WHITE, buf, sizeof(buf));
    check("PGN: two humans are 'Player 1'", strcmp(buf, "Player 1") == 0);
    players_pgn_name(hh, BLACK, buf, sizeof(buf));
    check("and 'Player 2'", strcmp(buf, "Player 2") == 0);

    players_matchup(he, buf, sizeof(buf));
    check("matchup: 'You vs dchess Hard'", strcmp(buf, "You vs dchess Hard") == 0);
    players_matchup(hh, buf, sizeof(buf));
    check("matchup: 'Player 1 vs Player 2'", strcmp(buf, "Player 1 vs Player 2") == 0);
    Player ee[2] = { E(DIFF_EASY), E(DIFF_HARD) };
    players_matchup(ee, buf, sizeof(buf));
    check("matchup: 'dchess Easy vs dchess Hard'",
          strcmp(buf, "dchess Easy vs dchess Hard") == 0);

    players_describe(he, buf, sizeof(buf));
    check("describe: 'White: You · Black: dchess Hard'",
          strcmp(buf, "White: You · Black: dchess Hard") == 0);
}

int main(void)
{
    test_constructors();
    test_automated();
    test_undo_plies();
    test_should_start();
    test_stats_entry();
    test_commands();
    test_labels();

    if (failures) {
        printf("\n%d player test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll player tests passed.\n");
    return 0;
}
