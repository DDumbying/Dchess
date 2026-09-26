/* Engine registry.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "utils/engines.h"

static int failures = 0;
static char dir[] = "/tmp/dchess-engines-XXXXXX";

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static EngineEntry entry(const char *name, const char *path, int depth, int ms, int elo)
{
    EngineEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "%s", name);
    snprintf(e.path, sizeof(e.path), "%s", path);
    e.limit_depth = depth;
    e.limit_ms    = ms;
    e.elo         = elo;
    return e;
}

static void write_conf(const char *text)
{
    char path[512], sub[512];
    snprintf(sub, sizeof(sub), "%s/dchess", dir);
    mkdir(sub, 0755);
    engines_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    fputs(text, f);
    fclose(f);
}

static void test_path_and_missing(void)
{
    printf("== path and missing file ==\n");
    char path[512], want[512];
    snprintf(want, sizeof(want), "%s/dchess/engines.conf", dir);
    check("the path follows XDG_CONFIG_HOME",
          engines_path(path, sizeof(path)) && strcmp(path, want) == 0);

    EngineList l;
    l.count = 5;
    check("a missing file loads as an empty list", engines_load(&l) == 0 && l.count == 0);
}

static void test_round_trip(void)
{
    printf("== save and load ==\n");
    char err[160];
    EngineList l = { .count = 0 };
    EngineEntry a = entry("Stockfish full", "stockfish", 0, 1000, 0);
    EngineEntry b = entry("Lc0", "/opt/lc0/lc0", 12, 1000, 0);
    EngineEntry c = entry("Stockfish 1500", "/usr/bin/stockfish", 0, 500, 1500);
    check("three entries are added",
          engines_add(&l, &a, err, sizeof(err)) && engines_add(&l, &b, err, sizeof(err)) &&
          engines_add(&l, &c, err, sizeof(err)) && l.count == 3);
    check("the file is written", engines_save(&l) == 1);

    EngineList back;
    check("and read back", engines_load(&back) == 1 && back.count == 3);
    check("in the same order",
          strcmp(back.e[0].name, "Stockfish full") == 0 &&
          strcmp(back.e[1].name, "Lc0") == 0 &&
          strcmp(back.e[2].name, "Stockfish 1500") == 0);
    check("with every field",
          strcmp(back.e[1].path, "/opt/lc0/lc0") == 0 && back.e[1].limit_depth == 12 &&
          back.e[2].limit_depth == 0 && back.e[2].limit_ms == 500 && back.e[2].elo == 1500 &&
          back.e[0].elo == 0);
    check("find by exact name", engines_find(&back, "Lc0") == &back.e[1]);
    check("find is case-sensitive", engines_find(&back, "lc0") == NULL);
}

static void test_names(void)
{
    printf("== names ==\n");
    char err[160];
    EngineList l = { .count = 0 };
    EngineEntry a = entry("Stockfish", "stockfish", 0, 1000, 0);
    engines_add(&l, &a, err, sizeof(err));

    check("a duplicate is refused",
          !engines_add(&l, &a, err, sizeof(err)) && strstr(err, "already") != NULL);
    EngineEntry r = entry("Hard", "x", 0, 1000, 0);
    check("'Hard' is reserved", !engines_add(&l, &r, err, sizeof(err)));
    EngineEntry h = entry("human", "x", 0, 1000, 0);
    check("'human' is reserved", !engines_add(&l, &h, err, sizeof(err)));
    EngineEntry empty = entry("", "x", 0, 1000, 0);
    check("an empty name is refused", !engines_add(&l, &empty, err, sizeof(err)));
    check("41 characters is too long",
          !engines_check_name(&l, "12345678901234567890123456789012345678901", -1, err, sizeof(err)));
    check("40 characters is fine",
          engines_check_name(&l, "1234567890123456789012345678901234567890", -1, err, sizeof(err)));
    check("brackets are refused", !engines_check_name(&l, "a]b", -1, err, sizeof(err)));
    check("an entry may keep its own name", engines_check_name(&l, "Stockfish", 0, err, sizeof(err)));
    EngineEntry nopath = entry("No path", "", 0, 1000, 0);
    check("an entry needs a path", !engines_add(&l, &nopath, err, sizeof(err)));
}

static void test_cap_remove_replace(void)
{
    printf("== cap, remove, replace ==\n");
    char err[160], name[16];
    EngineList l = { .count = 0 };
    for (int i = 0; i < ENGINES_MAX; i++) {
        snprintf(name, sizeof(name), "E%d", i);
        EngineEntry e = entry(name, "x", 0, 1000, 0);
        engines_add(&l, &e, err, sizeof(err));
    }
    EngineEntry extra = entry("Extra", "x", 0, 1000, 0);
    check("32 entries fit", l.count == ENGINES_MAX);
    check("a 33rd is refused", !engines_add(&l, &extra, err, sizeof(err)));

    check("remove by name", engines_remove(&l, "E1") == 1 && l.count == ENGINES_MAX - 1);
    check("keeps the order", strcmp(l.e[1].name, "E2") == 0);
    check("removing an unknown name is 0", engines_remove(&l, "Nope") == 0);

    EngineEntry clash = entry("E0", "y", 0, 1000, 0);
    check("replace refuses another entry's name", !engines_replace(&l, 1, &clash, err, sizeof(err)));
    EngineEntry renamed = entry("Renamed", "y", 5, 1000, 0);
    check("replace with a new name works",
          engines_replace(&l, 1, &renamed, err, sizeof(err)) &&
          strcmp(l.e[1].name, "Renamed") == 0 && l.e[1].limit_depth == 5);
}

static void test_malformed(void)
{
    printf("== a hand-edited file ==\n");
    write_conf("; my engines\n"
               "[Good]\n"
               "path = /bin/good\n"
               "limit = time 2000\n"
               "\n"
               "[No path]\n"
               "limit = depth 5\n"
               "\n"
               "garbage line\n"
               "[Bad limit]\n"
               "path = /bin/x\n"
               "limit = fast\n"
               "\n"
               "[Good]\n"
               "path = /bin/dup\n"
               "\n"
               "[Elo]\n"
               "path = eng ; trailing comment\n"
               "limit = depth 7\n"
               "elo = 1800\n");
    EngineList l;
    check("it still loads", engines_load(&l) == 1);
    check("the good sections survive", l.count == 3);
    check("Good keeps its first definition",
          strcmp(l.e[0].name, "Good") == 0 && strcmp(l.e[0].path, "/bin/good") == 0 &&
          l.e[0].limit_ms == 2000);
    check("a bad limit falls back to 1s",
          strcmp(l.e[1].name, "Bad limit") == 0 && l.e[1].limit_ms == 1000 &&
          l.e[1].limit_depth == 0);
    check("comments are stripped and fields read",
          strcmp(l.e[2].path, "eng") == 0 && l.e[2].limit_depth == 7 && l.e[2].elo == 1800);
}

static void test_labels(void)
{
    printf("== strength labels ==\n");
    char buf[48];
    EngineEntry e = entry("x", "x", 0, 1000, 0);
    engine_strength_label(&e, buf, sizeof(buf));
    check("1s/move", strcmp(buf, "1s/move") == 0);
    e.limit_ms = 500;
    engine_strength_label(&e, buf, sizeof(buf));
    check("0.5s/move", strcmp(buf, "0.5s/move") == 0);
    e.limit_depth = 12;
    engine_strength_label(&e, buf, sizeof(buf));
    check("depth 12", strcmp(buf, "depth 12") == 0);
    e.limit_depth = 0; e.limit_ms = 1000; e.elo = 1500;
    engine_strength_label(&e, buf, sizeof(buf));
    check("1s · 1500 Elo", strcmp(buf, "1s · 1500 Elo") == 0);
    e.limit_depth = 12; e.elo = 1800;
    engine_strength_label(&e, buf, sizeof(buf));
    check("depth 12 · 1800 Elo", strcmp(buf, "depth 12 · 1800 Elo") == 0);
}

static void test_save_failure(void)
{
    printf("== an unwritable config folder ==\n");
    char blocker[512];
    snprintf(blocker, sizeof(blocker), "%s/blocker", dir);
    FILE *f = fopen(blocker, "w");
    fclose(f);
    setenv("XDG_CONFIG_HOME", blocker, 1);   /* a file where a folder should be */
    EngineList l = { .count = 0 };
    check("save reports failure instead of crashing", engines_save(&l) == 0);
    setenv("XDG_CONFIG_HOME", dir, 1);
    remove(blocker);
}

int main(void)
{
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    setenv("XDG_CONFIG_HOME", dir, 1);

    test_path_and_missing();
    test_round_trip();
    test_names();
    test_cap_remove_replace();
    test_malformed();
    test_labels();
    test_save_failure();

    char path[512], sub[512];
    engines_path(path, sizeof(path));
    remove(path);
    snprintf(sub, sizeof(sub), "%s/dchess", dir);
    rmdir(sub);
    rmdir(dir);

    if (failures) {
        printf("\n%d registry test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll registry tests passed.\n");
    return 0;
}
