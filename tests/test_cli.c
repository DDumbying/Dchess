/* Command-line parsing.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "utils/cli.h"
#include "utils/constants.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Splits on spaces; argv[0] is the program name. */
static int parse(CliArgs *a, const char *line)
{
    static char buf[256];
    char *argv[32];
    int argc = 0;
    snprintf(buf, sizeof(buf), "%s", line);
    argv[argc++] = "dchess";
    for (char *t = strtok(buf, " "); t && argc < 32; t = strtok(NULL, " "))
        argv[argc++] = t;
    return cli_parse(argc, argv, a);
}

static int human(const Player *p)             { return p->kind == PLAYER_HUMAN; }
static int engine(const Player *p, int level) { return p->kind == PLAYER_BUILTIN && p->level == level; }

int main(void)
{
    CliArgs a;

    printf("== players ==\n");
    check("no flags parses", parse(&a, "") == 0);
    check("default: you as White against dchess Medium",
          human(&a.players[WHITE]) && engine(&a.players[BLACK], DIFF_MEDIUM));
    check("the default engine has medium's depth",
          a.players[BLACK].depth == cli_depth_for_difficulty(DIFF_MEDIUM));

    parse(&a, "-c black -d hard");
    check("-c black -d hard: engine White at hard, you Black",
          engine(&a.players[WHITE], DIFF_HARD) && human(&a.players[BLACK]));

    parse(&a, "-2");
    check("-2: two humans", human(&a.players[WHITE]) && human(&a.players[BLACK]));

    check("--white hard --black easy parses",
          parse(&a, "--white hard --black easy") == 0);
    check("and is engine against engine",
          engine(&a.players[WHITE], DIFF_HARD) && engine(&a.players[BLACK], DIFF_EASY));
    check("and skips the menu like any gameplay flag", a.any_gameplay_flag);

    parse(&a, "--black hard -2");
    check("--black overrides a later -2",
          human(&a.players[WHITE]) && engine(&a.players[BLACK], DIFF_HARD));
    parse(&a, "-2 --black hard");
    check("and an earlier one",
          human(&a.players[WHITE]) && engine(&a.players[BLACK], DIFF_HARD));
    parse(&a, "--white human -c black");
    check("--white overrides -c", human(&a.players[WHITE]) && human(&a.players[BLACK]));

    printf("== errors ==\n");
    check("a bad value is an error",
          parse(&a, "--white wizard") != 0 && a.error && strstr(a.error_msg, "wizard"));
    check("a missing value is an error", parse(&a, "--black") != 0 && a.error);

    if (failures) {
        printf("\n%d CLI test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll CLI tests passed.\n");
    return 0;
}
