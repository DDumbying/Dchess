/* Profiles and the first-run import.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "game/profiles.h"
#include "game/records.h"

static int failures = 0;
static char dir[] = "/tmp/dchess-profiles-XXXXXX";
static char games[512];

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void write_conf(const char *text)
{
    char path[512], sub[512];
    snprintf(sub, sizeof(sub), "%s/dchess", dir);
    mkdir(sub, 0755);
    profiles_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

static void remove_conf(void)
{
    char path[512];
    profiles_path(path, sizeof(path));
    remove(path);
}

static void test_round_trip(void)
{
    printf("== save and load ==\n");
    char err[128];
    ProfileList l = { .count = 0 }, back;
    remove_conf();
    check("a missing file loads as 0", profiles_load(&back) == 0 && back.count == 0);

    check("add saeed", profiles_add(&l, NULL, "saeed", err, sizeof(err)));
    check("add a name with a space", profiles_add(&l, NULL, "Sa Eed", err, sizeof(err)));
    l.active = 1;
    snprintf(l.p[0].theme, sizeof(l.p[0].theme), "catppuccin");
    snprintf(l.p[0].black, sizeof(l.p[0].black), "dchess hard");
    l.p[0].legacy_games = 5;
    l.p[0].legacy_wins = 3;
    check("saved", profiles_save(&l) == 1);
    check("loaded", profiles_load(&back) == 1 && back.count == 2);
    check("the active profile survives", back.active == 1);
    check("fields survive",
          strcmp(back.p[1].name, "Sa Eed") == 0 && strcmp(back.p[0].theme, "catppuccin") == 0 &&
          strcmp(back.p[0].black, "dchess hard") == 0 && back.p[0].legacy_games == 5 &&
          back.p[0].legacy_wins == 3);
    check("find by name", profiles_find(&back, "Sa Eed") == 1 && profiles_find(&back, "x") == -1);

    const char *names[4];
    check("names list the active profile first",
          profiles_names(&back, names, 4) == 2 && strcmp(names[0], "Sa Eed") == 0);
}

static void test_names(void)
{
    printf("== names ==\n");
    char err[128];
    ProfileList l = { .count = 0 };
    EngineList e;
    memset(&e, 0, sizeof(e));
    e.count = 1;
    snprintf(e.e[0].name, sizeof(e.e[0].name), "Fake");
    profiles_add(&l, &e, "saeed", err, sizeof(err));

    check("a duplicate is refused", !profiles_add(&l, &e, "saeed", err, sizeof(err)));
    check("'Guest' is reserved", !profiles_add(&l, &e, "Guest", err, sizeof(err)));
    check("'HARD' is reserved", !profiles_add(&l, &e, "HARD", err, sizeof(err)));
    check("25 characters is too long",
          !profiles_add(&l, &e, "1234567890123456789012345", err, sizeof(err)));
    check("brackets are refused", !profiles_add(&l, &e, "a]b", err, sizeof(err)));
    check("an engine's name is refused",
          !profiles_add(&l, &e, "Fake", err, sizeof(err)) && strstr(err, "engine") != NULL);
}

static void test_rename_remove(void)
{
    printf("== rename and remove ==\n");
    char err[128];
    ProfileList l = { .count = 0 };
    profiles_add(&l, NULL, "saeed", err, sizeof(err));
    profiles_add(&l, NULL, "alice", err, sizeof(err));
    records_append_legacy(games, "saeed", 1700000000L, 1);

    check("rename", profiles_rename(&l, NULL, 0, "neo", games, err, sizeof(err)) == 1);
    check("the list follows", profiles_find(&l, "neo") == 0);
    RecordList r;
    records_load(games, &r);
    check("the games follow", r.count == 1 && strcmp(r.r[0].white, "neo") == 0);
    records_free(&r);
    check("renaming onto another profile is refused",
          !profiles_rename(&l, NULL, 0, "alice", games, err, sizeof(err)));

    l.active = 1;
    check("remove the active profile", profiles_remove(&l, 1) == 1 && l.count == 1);
    check("another becomes active", l.active == 0);
    check("the last profile cannot be removed", profiles_remove(&l, 0) == 0 && l.count == 1);
}

static void test_malformed(void)
{
    printf("== a hand-edited file ==\n");
    write_conf("active = ghost\n"
               "garbage\n"
               "[saeed]\n"
               "theme = gruvbox\n"
               "legacy = 1 2\n"
               "[a;b]\n"
               "theme = x\n"
               "[alice]\n");
    ProfileList l;
    check("it loads", profiles_load(&l) == 1);
    check("the good sections survive", l.count == 2 && strcmp(l.p[1].name, "alice") == 0);
    check("a bad legacy line is ignored", l.p[0].legacy_games == 0);
    check("an unknown active falls back to the first", l.active == 0);
}

static void test_first_run(void)
{
    printf("== first run ==\n");
    ProfileList l;
    DchessStats old;
    memset(&old, 0, sizeof(old));
    old.games_played[0] = 2; old.games_played[1] = 3;
    old.wins[0] = 1; old.wins[1] = 2;
    old.losses[1] = 1; old.draws[0] = 1;
    old.history_count = 3;
    old.history[0] = (GameRecord){ 1700000000L, 1 };
    old.history[1] = (GameRecord){ 1700000600L, -1 };
    old.history[2] = (GameRecord){ 1700001200L, 0 };

    remove_conf();
    remove(games);
    check("it runs when there is no file", profiles_first_run(&l, "saeed", &old, games) == 1);
    check("one active profile named after the user",
          l.count == 1 && l.active == 0 && strcmp(l.p[0].name, "saeed") == 0);
    check("the old totals become legacy",
          l.p[0].legacy_games == 5 && l.p[0].legacy_wins == 3 &&
          l.p[0].legacy_losses == 1 && l.p[0].legacy_draws == 1);
    RecordList r;
    records_load(games, &r);
    check("each dated result becomes a legacy record", r.count == 3 && r.r[0].legacy);
    records_free(&r);
    check("it is saved", profiles_load(&l) == 1 && l.count == 1);
    check("a second run does nothing", profiles_first_run(&l, "bob", &old, games) == 0 &&
          l.count == 1 && strcmp(l.p[0].name, "saeed") == 0);

    remove_conf();
    check("an invalid user name becomes 'player'",
          profiles_first_run(&l, "root;x", NULL, games) == 1 && strcmp(l.p[0].name, "player") == 0);
    remove_conf();
    check("so does an empty one",
          profiles_first_run(&l, "", NULL, games) == 1 && strcmp(l.p[0].name, "player") == 0);
    remove_conf();
    check("and a missing one",
          profiles_first_run(&l, NULL, NULL, games) == 1 && strcmp(l.p[0].name, "player") == 0);
}


static void test_stats(void)
{
    printf("== stats view ==\n");
    Profile p;
    memset(&p, 0, sizeof(p));
    snprintf(p.name, sizeof(p.name), "saeed");
    p.legacy_games = 4; p.legacy_wins = 2; p.legacy_losses = 1; p.legacy_draws = 1;
    remove(games);
    records_append_legacy(games, "saeed", 1700000000L, 1);
    FILE *f = fopen(games, "a");
    fputs("\n[Event \"x\"]\n[White \"saeed\"]\n[Black \"alice\"]\n[Result \"1-0\"]\n"
          "[WhiteKind \"profile\"]\n[BlackKind \"profile\"]\n\n1-0\n", f);
    fclose(f);
    DchessStats s;
    profiles_stats(&p, games, &s);
    int total = s.games_played[0] + s.games_played[1] + s.games_played[2];
    int wins = s.wins[0] + s.wins[1] + s.wins[2];
    check("legacy totals and a game against a person are counted", total == 5 && wins == 3);
    check("legacy records feed the history", s.history_count == 2 && s.history[0].result == 1);
}

int main(void)
{
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    setenv("XDG_CONFIG_HOME", dir, 1);
    snprintf(games, sizeof(games), "%s/games.pgn", dir);

    test_round_trip();
    test_names();
    test_rename_remove();
    test_malformed();
    test_first_run();
    test_stats();

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) printf("  (could not clean %s)\n", dir);

    if (failures) {
        printf("\n%d profile test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll profile tests passed.\n");
    return 0;
}
