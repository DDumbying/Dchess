/* Command-line parsing.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "utils/cli.h"
#include "utils/constants.h"
#include "utils/engines.h"
#include "game/profiles.h"
#include <stdlib.h>
#include <unistd.h>

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

static char dir[] = "/tmp/dchess-cli-XXXXXX";

static void add(EngineList *l, const char *name, const char *path)
{
    char err[128];
    EngineEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "%s", name);
    snprintf(e.path, sizeof(e.path), "%s", path);
    e.limit_ms = 1000;
    engines_add(l, &e, err, sizeof(err));
}

static void test_engines(void)
{
    printf("== engines ==\n");
    CliArgs a;
    if (!mkdtemp(dir)) return;
    setenv("XDG_CONFIG_HOME", dir, 1);

    check("with no registry, a name is an error that says so",
          parse(&a, "--white Fake") != 0 && strstr(a.error_msg, "no engines registered") != NULL);

    EngineList l = { .count = 0 };
    add(&l, "Stockfish 1500", "stockfish");
    add(&l, "Fake", "build/fake_uci");
    engines_save(&l);

    check("--white Fake picks the registered engine",
          parse(&a, "--white Fake --black hard") == 0 &&
          a.players[WHITE].kind == PLAYER_UCI && strcmp(a.players[WHITE].name, "Fake") == 0 &&
          engine(&a.players[BLACK], DIFF_HARD));

    char *av[] = { "dchess", "--black", "Stockfish 1500" };
    check("a name with spaces, as one argument",
          cli_parse(3, av, &a) == 0 && a.players[BLACK].kind == PLAYER_UCI &&
          strcmp(a.players[BLACK].name, "Stockfish 1500") == 0);

    check("an unknown name lists the registered ones",
          parse(&a, "--white Komodo") != 0 && strstr(a.error_msg, "Komodo") &&
          strstr(a.error_msg, "Stockfish 1500") && strstr(a.error_msg, "Fake"));
    check("'hard' still means the built-in engine",
          parse(&a, "--white hard") == 0 && engine(&a.players[WHITE], DIFF_HARD));
    check("--engines asks for the list", parse(&a, "--engines") == 0 && a.list_engines);
    check("and is off otherwise", parse(&a, "") == 0 && !a.list_engines);

    char path[512], sub[512];
    engines_path(path, sizeof(path));
    remove(path);
    snprintf(sub, sizeof(sub), "%s/dchess", dir);
    rmdir(sub);
    rmdir(dir);
}


static void test_profiles_cli(void)
{
    printf("== profiles ==\n");
    static char pdir[] = "/tmp/dchess-cli-prof-XXXXXX";
    char err[128];
    CliArgs a;
    if (!mkdtemp(pdir)) return;
    setenv("XDG_CONFIG_HOME", pdir, 1);
    ProfileList l = { .count = 0 };
    profiles_add(&l, NULL, "saeed", err, sizeof(err));
    profiles_add(&l, NULL, "alice", err, sizeof(err));
    profiles_save(&l);

    check("the default human side is the active profile",
          parse(&a, "") == 0 && a.human_active[WHITE] && !a.human_active[BLACK]);
    check("--profile alice", parse(&a, "--profile alice") == 0 && strcmp(a.profile, "alice") == 0);
    check("an unknown --profile lists the profiles",
          parse(&a, "--profile bob") != 0 && strstr(a.error_msg, "saeed") && strstr(a.error_msg, "alice"));
    check("--profiles asks for the list", parse(&a, "--profiles") == 0 && a.list_profiles);
    check("--white alice is that profile",
          parse(&a, "--white alice") == 0 && human(&a.players[WHITE]) &&
          strcmp(a.players[WHITE].name, "alice") == 0 && !a.human_active[WHITE]);
    check("--black guest is a guest",
          parse(&a, "--black guest") == 0 && human(&a.players[BLACK]) &&
          a.players[BLACK].name[0] == '\0' && !a.human_active[BLACK]);
    check("--white human is the active profile", parse(&a, "--white human") == 0 && a.human_active[WHITE]);
    check("--theme marks the theme as chosen", parse(&a, "--theme btop") == 0 && a.theme_set);
    check("otherwise it is not", parse(&a, "--white easy") == 0 && !a.theme_set);
    check("--white easy is still dchess", parse(&a, "--white easy") == 0 && engine(&a.players[WHITE], DIFF_EASY));

    char path[512], cmd[600];
    profiles_path(path, sizeof(path));
    snprintf(cmd, sizeof(cmd), "rm -rf %s", pdir);
    if (system(cmd) != 0) printf("  (could not clean %s)\n", pdir);
}

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

    test_engines();
    test_profiles_cli();

    if (failures) {
        printf("\n%d CLI test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll CLI tests passed.\n");
    return 0;
}
