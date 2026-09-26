# UCI Engines Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Register external UCI engines as named setups, and let them play either side of the board, through the start menu, the command line and in-game commands.

**Architecture:**
- A text registry, `src/utils/engines.c`, stores the named setups.
- A UCI driver, `src/game/uci.c`, sits behind the existing `Opponent` interface. It uses `fork`/`exec` with non-blocking pipes, polled on the existing 100 ms tick, with no threads. The `Opponent` interface gains a small vtable, so the built-in engine and UCI engines share it.
- The TUI adds a `PLAYER_UCI` kind, an Engines screen, and failure handling.

**Tech Stack:** C (gcc -O2 -Wall), POSIX processes and pipes, ncursesw, plain `check()` test binaries run by `make test`.

**Spec:** `docs/superpowers/specs/2026-09-26-uci-engines-design.md`

## Global Constraints

- The build must produce **zero warnings** under `gcc -O2 -Wall`. `CFLAGS` has `-D_XOPEN_SOURCE=600`, so use `pipe` with `fcntl(FD_CLOEXEC)`, not `pipe2`.
- Tests link only `CORE_SRC` (`src/engine`, `src/game`, `src/utils`). `engines.c` and `uci.c` must not include ncurses or anything under `tui/`.
- Keep comments sparse, around 5–10% of lines, in the repo's style: short, and saying why, not what.
- Commits carry **no** `Co-Authored-By` trailer.
- The commit style is conventional: `feat:`, `fix:`, `docs:`, `test:`, with an optional scope such as `feat(tui):`.
- **Registry file:** `$XDG_CONFIG_HOME/dchess/engines.conf`, else `~/.config/dchess/engines.conf`. At most 32 entries.
- **Names:** 1–40 characters, unique, and never `easy`, `medium`, `hard` or `human` (case-insensitive).
- **Limits:** `time <ms>` (100–60000) or `depth <n>` (1–30), defaulting to `time 1000`. `elo` is optional.
- **Timeouts:** handshake (`uciok`/`readyok`) 10 s; Engines-screen probe 5 s; `stop` 2 s; `quit` grace 0.5 s, then `SIGKILL`.
- **Error messages** are exactly `<name>: could not start <path>`, `<name>: no reply from engine`, `<name>: engine exited`, `<name>: played illegal move <m>` and `<name>: did not stop`.
- **Engine processes:** the engine's stderr goes to `/dev/null`, and dchess ignores `SIGPIPE`.
- **Header changes:** `make` only rebuilds `dchess` when a `.c` file changes, so after header edits build with `make -B dchess` and test with `make -B test`.
- **Fake engine:** `build/fake_uci` picks its mode from `$FAKE_UCI_MODE`, or from `argv[1]` for manual runs. When `$FAKE_UCI_LOG` is set, it appends every line it reads there.
  - The spec says "first argument". An `EngineEntry` path cannot carry arguments, so the environment is what the driver tests and tmux runs can actually reach.
  - Logging every line, rather than only `setoption` lines, also lets the tests check the `position`, `go` and `ucinewgame` lines.

## Review Focus

Most likely first:

1. **Pointing dchess at something that is not a UCI engine** (`/bin/true`, `/bin/cat`). A clean error, and no hang beyond the timeout. Tested in Task 4 (`test_probe_not_an_engine`).
2. **An engine that prints very long lines**, such as a huge `info string`. No overflow or hang, and the search still completes. Tested in Task 4 with the `chatty` fake mode.
3. **Quitting dchess while a UCI engine is mid-search or mid-handshake.** No orphaned engine process is left. Verified in Task 7, Step 6, scenario D.
4. **`undo`, `new` or a player change while a UCI engine is thinking.** The search is stopped and drained, and no stale move lands. Verified in Task 7, Step 6, scenario E.
5. **A config folder that cannot be written.** Saving reports failure instead of crashing. Tested in Task 1 (`test_save_failure`).

---

### Task 1: Engine registry

**Files:**
- Create: `headers/utils/engines.h`
- Create: `src/utils/engines.c`
- Test: `tests/test_engines.c`

**Interfaces:**
- Consumes: nothing.
- Produces:
  ```c
  #define ENGINES_MAX 32
  #define ENGINE_NAME_MAX 40
  typedef struct { char name[ENGINE_NAME_MAX + 1]; char path[256];
                   int limit_depth; int limit_ms; int elo; } EngineEntry;
  typedef struct { EngineEntry e[ENGINES_MAX]; int count; } EngineList;
  int  engines_path(char *buf, size_t n);
  int  engines_load(EngineList *l);          /* 1 if the file was read */
  int  engines_save(const EngineList *l);    /* 1 on success */
  int  engines_check_name(const EngineList *l, const char *name, int skip, char *err, size_t n);
  int  engines_add(EngineList *l, const EngineEntry *e, char *err, size_t n);
  int  engines_replace(EngineList *l, int index, const EngineEntry *e, char *err, size_t n);
  int  engines_remove(EngineList *l, const char *name);
  const EngineEntry *engines_find(const EngineList *l, const char *name);
  void engine_strength_label(const EngineEntry *e, char *buf, size_t n);
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/test_engines.c`:

```c
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_engines 2>&1 | grep -m1 error`
Expected: `utils/engines.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `headers/utils/engines.h`:

```c
#ifndef ENGINES_H
#define ENGINES_H

#include <stddef.h>

#define ENGINES_MAX     32
#define ENGINE_NAME_MAX 40

/* One named setup from engines.conf. */
typedef struct {
    char name[ENGINE_NAME_MAX + 1];
    char path[256];
    int  limit_depth;   /* 0 = limited by time instead */
    int  limit_ms;
    int  elo;           /* 0 = full strength */
} EngineEntry;

typedef struct {
    EngineEntry e[ENGINES_MAX];
    int         count;
} EngineList;

/* $XDG_CONFIG_HOME/dchess/engines.conf, else ~/.config/dchess/engines.conf.
 * 0 when neither variable is set. */
int  engines_path(char *buf, size_t n);

/* 1 if the file was read. A missing file is an empty list; bad lines are
 * skipped. */
int  engines_load(EngineList *l);
int  engines_save(const EngineList *l);

/* Whether `name` may be used by any entry other than number `skip`
 * (-1 for none). `err` may be NULL. */
int  engines_check_name(const EngineList *l, const char *name, int skip,
                        char *err, size_t n);
int  engines_add(EngineList *l, const EngineEntry *e, char *err, size_t n);
int  engines_replace(EngineList *l, int index, const EngineEntry *e,
                     char *err, size_t n);
int  engines_remove(EngineList *l, const char *name);
const EngineEntry *engines_find(const EngineList *l, const char *name);

/* "1s/move", "depth 12", "1s · 1500 Elo" */
void engine_strength_label(const EngineEntry *e, char *buf, size_t n);

#endif
```

- [ ] **Step 4: Write the implementation**

Create `src/utils/engines.c`:

```c
#include "utils/engines.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *RESERVED[] = { "easy", "medium", "hard", "human" };

int engines_path(char *buf, size_t n)
{
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])        snprintf(buf, n, "%s/dchess/engines.conf", xdg);
    else if (home && home[0]) snprintf(buf, n, "%s/.config/dchess/engines.conf", home);
    else return 0;
    return 1;
}

static void set_err(char *err, size_t n, const char *msg)
{
    if (err && n) snprintf(err, n, "%s", msg);
}

int engines_check_name(const EngineList *l, const char *name, int skip,
                       char *err, size_t n)
{
    size_t len = strlen(name);
    if (len == 0 || len > ENGINE_NAME_MAX) {
        set_err(err, n, "a name needs 1 to 40 characters");
        return 0;
    }
    if (isspace((unsigned char)name[0]) || isspace((unsigned char)name[len - 1])) {
        set_err(err, n, "a name cannot start or end with a space");
        return 0;
    }
    if (strpbrk(name, "[];")) {
        set_err(err, n, "a name cannot contain [ ] or ;");
        return 0;
    }
    for (size_t i = 0; i < sizeof(RESERVED) / sizeof(RESERVED[0]); i++)
        if (strcasecmp(name, RESERVED[i]) == 0) {
            set_err(err, n, "that name is reserved for dchess's own players");
            return 0;
        }
    for (int i = 0; i < l->count; i++)
        if (i != skip && strcmp(l->e[i].name, name) == 0) {
            set_err(err, n, "an engine with that name already exists");
            return 0;
        }
    return 1;
}

int engines_add(EngineList *l, const EngineEntry *e, char *err, size_t n)
{
    if (l->count >= ENGINES_MAX) {
        set_err(err, n, "the list is full (32 engines)");
        return 0;
    }
    if (!engines_check_name(l, e->name, -1, err, n)) return 0;
    if (!e->path[0]) {
        set_err(err, n, "an engine needs a path");
        return 0;
    }
    l->e[l->count++] = *e;
    return 1;
}

int engines_replace(EngineList *l, int index, const EngineEntry *e,
                    char *err, size_t n)
{
    if (index < 0 || index >= l->count) {
        set_err(err, n, "no such engine");
        return 0;
    }
    if (!engines_check_name(l, e->name, index, err, n)) return 0;
    if (!e->path[0]) {
        set_err(err, n, "an engine needs a path");
        return 0;
    }
    l->e[index] = *e;
    return 1;
}

int engines_remove(EngineList *l, const char *name)
{
    for (int i = 0; i < l->count; i++) {
        if (strcmp(l->e[i].name, name) != 0) continue;
        memmove(&l->e[i], &l->e[i + 1], (size_t)(l->count - i - 1) * sizeof(l->e[0]));
        l->count--;
        return 1;
    }
    return 0;
}

const EngineEntry *engines_find(const EngineList *l, const char *name)
{
    for (int i = 0; i < l->count; i++)
        if (strcmp(l->e[i].name, name) == 0) return &l->e[i];
    return NULL;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static void parse_limit(const char *v, EngineEntry *e)
{
    char kind[8];
    int x;
    if (sscanf(v, "%7s %d", kind, &x) != 2) return;
    if (strcmp(kind, "time") == 0 && x >= 100 && x <= 60000) {
        e->limit_ms = x;
        e->limit_depth = 0;
    } else if (strcmp(kind, "depth") == 0 && x >= 1 && x <= 30) {
        e->limit_depth = x;
    }
}

/* A section only joins the list once it is complete and valid. */
static void keep(EngineList *l, const EngineEntry *e, int open)
{
    if (open && e->path[0] && l->count < ENGINES_MAX &&
        engines_check_name(l, e->name, -1, NULL, 0))
        l->e[l->count++] = *e;
}

int engines_load(EngineList *l)
{
    char path[512], line[512];
    l->count = 0;
    if (!engines_path(path, sizeof(path))) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    EngineEntry cur;
    int open = 0;
    memset(&cur, 0, sizeof(cur));

    while (fgets(line, sizeof(line), f)) {
        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';
        char *s = trim(line);
        if (!*s) continue;

        if (*s == '[') {
            keep(l, &cur, open);
            open = 0;
            char *close = strchr(s, ']');
            if (!close) continue;
            *close = '\0';
            char *name = trim(s + 1);
            if (strlen(name) > ENGINE_NAME_MAX) continue;
            memset(&cur, 0, sizeof(cur));
            cur.limit_ms = 1000;
            snprintf(cur.name, sizeof(cur.name), "%s", name);
            open = 1;
            continue;
        }
        char *eq = strchr(s, '=');
        if (!open || !eq) continue;
        *eq = '\0';
        char *key = trim(s), *val = trim(eq + 1);
        if (strcmp(key, "path") == 0 && *val && strlen(val) < sizeof(cur.path))
            snprintf(cur.path, sizeof(cur.path), "%s", val);
        else if (strcmp(key, "limit") == 0)
            parse_limit(val, &cur);
        else if (strcmp(key, "elo") == 0 && atoi(val) > 0)
            cur.elo = atoi(val);
    }
    keep(l, &cur, open);
    fclose(f);
    return 1;
}

static int make_dirs(const char *file)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", file);
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return 0;
        *p = '/';
    }
    return 1;
}

int engines_save(const EngineList *l)
{
    char path[512];
    if (!engines_path(path, sizeof(path)) || !make_dirs(path)) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    for (int i = 0; i < l->count; i++) {
        const EngineEntry *e = &l->e[i];
        fprintf(f, "%s[%s]\n", i ? "\n" : "", e->name);
        fprintf(f, "path  = %s\n", e->path);
        if (e->limit_depth) fprintf(f, "limit = depth %d\n", e->limit_depth);
        else                fprintf(f, "limit = time %d\n", e->limit_ms);
        if (e->elo)         fprintf(f, "elo   = %d\n", e->elo);
    }
    int ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

void engine_strength_label(const EngineEntry *e, char *buf, size_t n)
{
    char base[24];
    if (e->limit_depth)
        snprintf(base, sizeof(base), "depth %d", e->limit_depth);
    else if (e->limit_ms % 1000 == 0)
        snprintf(base, sizeof(base), "%ds", e->limit_ms / 1000);
    else
        snprintf(base, sizeof(base), "%.1fs", e->limit_ms / 1000.0);

    if (e->elo)              snprintf(buf, n, "%s · %d Elo", base, e->elo);
    else if (e->limit_depth) snprintf(buf, n, "%s", base);
    else                     snprintf(buf, n, "%s/move", base);
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/test_engines 2>&1 | grep -c warning; ./build/test_engines | tail -1`
Expected: `0`, then `All registry tests passed.`

- [ ] **Step 6: Run the whole suite**

Run: `make -B test 2>&1 | tail -1`
Expected: `All suites passed.`

- [ ] **Step 7: Commit**

```bash
git add headers/utils/engines.h src/utils/engines.c tests/test_engines.c
git commit -m "feat: registry of named UCI engine setups"
```

---

### Task 2: `Opponent` becomes an interface, and `start` takes the game

**Files:**
- Create: `headers/game/opponent_impl.h`
- Modify: `headers/game/opponent.h` (replace it)
- Modify: `src/game/opponent.c` (replace it)
- Modify: `src/tui/commands.c:194` (the `opponent_start` call)
- Test: `tests/test_opponent.c` (replace it)

**Interfaces:**
- Consumes: `GameState`, `game_hash()` from `game/game.h`.
- Produces:
  ```c
  int  opponent_start(Opponent *o, const GameState *g);   /* key = game_hash(g) */
  const char *opponent_error(const Opponent *o);          /* NULL unless failed */
  /* opponent_impl.h, for driver implementations only: */
  typedef struct {
      int  (*start)(Opponent *o, const GameState *g);
      int  (*poll)(Opponent *o, SearchResult *out, U64 *key);
      void (*stop)(Opponent *o);
      void (*cancel)(Opponent *o);
      void (*destroy)(Opponent *o);
      const char *(*error)(const Opponent *o);
  } OpponentOps;
  struct Opponent { const OpponentOps *ops; };   /* first member of every driver */
  ```

- [ ] **Step 1: Write the failing test**

Replace `tests/test_opponent.c` with:

```c
/* The built-in engine driver.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <time.h>
#include "game/opponent.h"
#include "game/game.h"
#include "engine/move.h"
#include "utils/bitboard.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void nap(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

static int wait_result(Opponent *o, SearchResult *r, U64 *key, long budget_ms)
{
    long t0 = now_ms();
    while (now_ms() - t0 < budget_ms) {
        if (opponent_poll(o, r, key)) return 1;
        nap(5);
    }
    return 0;
}

static void test_finds_mate(void)
{
    printf("== a search from start to result ==\n");
    GameState g;
    game_reset(&g);
    game_load_fen(&g, "6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");

    SearchResult r;
    U64 key = 0;
    Opponent *o = opponent_builtin(3, 5000);
    check("the driver is created", o != NULL);
    check("poll before any start returns 0", !opponent_poll(o, &r, &key));
    check("start is accepted", opponent_start(o, &g));
    check("a second start while busy is refused", !opponent_start(o, &g));
    check("a result arrives", wait_result(o, &r, &key, 10000));
    check("it is the back-rank mate Ra1-a8", FROM(r.best_move) == 0 && TO(r.best_move) == 56);
    check("the key is the game's hash", key == game_hash(&g));
    check("the built-in engine never fails", opponent_error(o) == NULL);
    check("the driver is idle again", !opponent_poll(o, &r, &key));
    opponent_free(o);
}

static void test_cancel(void)
{
    printf("== cancel ==\n");
    GameState g;
    game_reset(&g);

    SearchResult r;
    U64 key;
    Opponent *o = opponent_builtin(20, 20000);
    opponent_start(o, &g);
    long t0 = now_ms();
    opponent_cancel(o);
    check("cancel straight after start returns within 1s", now_ms() - t0 < 1000);
    check("and leaves no result to poll", !opponent_poll(o, &r, &key));
    check("the driver can start again", opponent_start(o, &g));
    opponent_cancel(o);
    opponent_free(o);
    opponent_free(NULL);
    check("freeing NULL is harmless", 1);
}

static void test_stop(void)
{
    printf("== stop ==\n");
    GameState g;
    game_reset(&g);

    SearchResult r;
    U64 key = 0;
    Opponent *o = opponent_builtin(20, 20000);
    opponent_start(o, &g);
    nap(200);
    long t0 = now_ms();
    opponent_stop(o);
    check("stop returns within 1s", now_ms() - t0 < 1000);
    check("the next poll has a result", opponent_poll(o, &r, &key));
    check("with the key and a move", key == game_hash(&g) && r.best_move != 0);
    opponent_free(o);
}

int main(void)
{
    init_attacks();

    test_finds_mate();
    test_cancel();
    test_stop();

    if (failures) {
        printf("\n%d opponent test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll opponent tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_opponent 2>&1 | grep -m1 error`
Expected: `too few arguments to function 'opponent_start'`, or an implicit declaration of `opponent_error`.

- [ ] **Step 3: Write the headers**

Replace `headers/game/opponent.h` with:

```c
#ifndef OPPONENT_H
#define OPPONENT_H

#include "engine/search.h"
#include "game/game.h"
#include "utils/types.h"

/* An engine playing one side. Searches run in the background: start one,
 * then poll from the main loop until its result comes back. */
typedef struct Opponent Opponent;

Opponent *opponent_builtin(int depth, int time_ms);

/* 0 while a search is already running. */
int  opponent_start (Opponent *o, const GameState *g);

/* 1 once a result is ready, with game_hash() of the game it was started
 * for. A failed search polls as a result with no move. */
int  opponent_poll  (Opponent *o, SearchResult *out, U64 *key);

/* Finish now; the next poll returns the best move found so far. */
void opponent_stop  (Opponent *o);

/* Stop and throw the result away. */
void opponent_cancel(Opponent *o);
void opponent_free  (Opponent *o);

/* NULL, or why the engine stopped working. */
const char *opponent_error(const Opponent *o);

#endif
```

Create `headers/game/opponent_impl.h`:

```c
#ifndef OPPONENT_IMPL_H
#define OPPONENT_IMPL_H

#include "game/opponent.h"

/* For driver implementations only. Each driver's struct starts with a
 * struct Opponent, so a pointer to one is a pointer to the other. */
typedef struct {
    int  (*start)(Opponent *o, const GameState *g);
    int  (*poll)(Opponent *o, SearchResult *out, U64 *key);
    void (*stop)(Opponent *o);
    void (*cancel)(Opponent *o);
    void (*destroy)(Opponent *o);
    const char *(*error)(const Opponent *o);
} OpponentOps;

struct Opponent { const OpponentOps *ops; };

#endif
```

- [ ] **Step 4: Rewrite the built-in driver behind the interface**

Replace `src/game/opponent.c` with:

```c
#include "game/opponent_impl.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

int opponent_start(Opponent *o, const GameState *g)            { return o->ops->start(o, g); }
int opponent_poll(Opponent *o, SearchResult *out, U64 *key)    { return o->ops->poll(o, out, key); }
void opponent_stop(Opponent *o)                                { o->ops->stop(o); }
void opponent_cancel(Opponent *o)                              { o->ops->cancel(o); }
void opponent_free(Opponent *o)                                { if (o) o->ops->destroy(o); }
const char *opponent_error(const Opponent *o)                  { return o->ops->error(o); }

/* busy and threaded belong to the owning thread. ready and result are
 * the worker's handoff and are guarded by mutex. */
typedef struct {
    Opponent        base;
    int             depth, time_ms;
    int             busy;
    int             threaded;
    pthread_t       thread;
    pthread_mutex_t mutex;
    int             ready;
    SearchResult    result;
    Position        snapshot;   /* the worker's private copy */
    U64             key;
} Builtin;

static void *worker(void *arg)
{
    Builtin *b = arg;
    SearchResult r = search(&b->snapshot, b->depth, b->time_ms);

    pthread_mutex_lock(&b->mutex);
    b->result = r;
    b->ready  = 1;
    pthread_mutex_unlock(&b->mutex);
    return NULL;
}

static int is_ready(Builtin *b)
{
    pthread_mutex_lock(&b->mutex);
    int r = b->ready;
    pthread_mutex_unlock(&b->mutex);
    return r;
}

static int builtin_start(Opponent *o, const GameState *g)
{
    Builtin *b = (Builtin *)o;
    if (b->busy) return 0;
    b->snapshot = g->pos;
    b->key      = game_hash(g);
    b->ready    = 0;
    b->busy     = 1;
    b->threaded = (pthread_create(&b->thread, NULL, worker, b) == 0);
    if (!b->threaded) worker(b);   /* no thread to be had: search here instead */
    return 1;
}

static void finish(Builtin *b)
{
    if (!b->threaded) return;
    /* search() clears any earlier cancel as it begins, so one sent before
     * the worker got that far would be lost. Keep asking until it answers. */
    while (!is_ready(b)) {
        search_cancel();
        struct timespec ms = { 0, 1000000L };
        nanosleep(&ms, NULL);
    }
    pthread_join(b->thread, NULL);
    b->threaded = 0;
}

static int builtin_poll(Opponent *o, SearchResult *out, U64 *key)
{
    Builtin *b = (Builtin *)o;
    if (!b->busy || !is_ready(b)) return 0;
    if (b->threaded) {
        pthread_join(b->thread, NULL);
        b->threaded = 0;
    }
    if (out) *out = b->result;
    if (key) *key = b->key;
    b->busy = 0;
    return 1;
}

static void builtin_stop(Opponent *o)
{
    Builtin *b = (Builtin *)o;
    if (b->busy) finish(b);
}

static void builtin_cancel(Opponent *o)
{
    Builtin *b = (Builtin *)o;
    if (!b->busy) return;
    finish(b);
    b->busy  = 0;
    b->ready = 0;
}

static void builtin_destroy(Opponent *o)
{
    Builtin *b = (Builtin *)o;
    builtin_cancel(o);
    pthread_mutex_destroy(&b->mutex);
    free(b);
}

static const char *builtin_error(const Opponent *o)
{
    (void)o;
    return NULL;
}

static const OpponentOps builtin_ops = {
    builtin_start, builtin_poll, builtin_stop,
    builtin_cancel, builtin_destroy, builtin_error,
};

Opponent *opponent_builtin(int depth, int time_ms)
{
    Builtin *b = calloc(1, sizeof(*b));
    if (!b) return NULL;
    b->base.ops = &builtin_ops;
    b->depth    = depth;
    b->time_ms  = time_ms;
    pthread_mutex_init(&b->mutex, NULL);
    return &b->base;
}
```

In `src/tui/commands.c`, `start_thinking`, replace:

```c
    if (!opponent_start(o, &state->game.pos, game_hash(&state->game))) return;
```
with:
```c
    if (!opponent_start(o, &state->game)) return;
```

- [ ] **Step 5: Run the tests and the build**

Run: `make build/test_opponent 2>&1 | grep -c warning; ./build/test_opponent | tail -1; make -B dchess 2>&1 | grep -c warning; make -B test 2>&1 | tail -1`
Expected: `0`, `All opponent tests passed.`, `0`, `All suites passed.`

- [ ] **Step 6: Commit**

```bash
git add headers/game/opponent.h headers/game/opponent_impl.h src/game/opponent.c src/tui/commands.c tests/test_opponent.c
git commit -m "refactor: opponent drivers behind an interface that takes the whole game"
```

---

### Task 3: UCI parsers and the position command

**Files:**
- Create: `headers/game/uci.h`
- Create: `src/game/uci.c`
- Test: `tests/test_uci.c`

**Interfaces:**
- Consumes: `GameState` (`start_fen`, `log_start`, `move_count`, `move_made`), `move_to_str()`, `MATE_SCORE`.
- Produces:
  ```c
  #define UCI_COMMAND_MAX 8448
  typedef struct { int depth, has_score, score_cp, is_mate, mate_in; long nodes, nps; } UciInfo;
  int  uci_parse_info(const char *line, UciInfo *out);
  int  uci_parse_bestmove(const char *line, char *move, size_t n);
  int  uci_parse_option(const char *line, char *name, size_t n, int *min, int *max);
  void uci_position_command(const GameState *g, char *buf, size_t n);
  int  uci_info_score(const UciInfo *info);
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/test_uci.c`:

```c
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_uci 2>&1 | grep -m1 error`
Expected: `game/uci.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `headers/game/uci.h`:

```c
#ifndef UCI_H
#define UCI_H

#include "game/game.h"
#include <stddef.h>

/* Room for "position fen ..." plus MAX_MOVE_HISTORY moves. */
#define UCI_COMMAND_MAX 8448

/* What an "info" line said. Fields it did not mention are 0. */
typedef struct {
    int  depth;
    int  has_score;
    int  score_cp;
    int  is_mate;
    int  mate_in;     /* moves; negative when the side to move is mated */
    long nodes;
    long nps;
} UciInfo;

int  uci_parse_info(const char *line, UciInfo *out);

/* "(none)" and a bare "bestmove" give "". */
int  uci_parse_bestmove(const char *line, char *move, size_t n);

/* min and max are 0 when the option gives none. */
int  uci_parse_option(const char *line, char *name, size_t n, int *min, int *max);

/* "position startpos|fen <start> [moves ...]" from the game log. */
void uci_position_command(const GameState *g, char *buf, size_t n);

/* Centipawns from the side to move's view; a mate is a large score, the
 * way search() reports one. */
int  uci_info_score(const UciInfo *info);

#endif
```

- [ ] **Step 4: Write the parsers**

Create `src/game/uci.c`:

```c
#include "game/uci.h"
#include "engine/move.h"
#include "utils/constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UCI_MAX_TOKENS 256

/* Splits a copy of `line` on whitespace. */
static int tokenize(const char *line, char *copy, size_t cn, char **tok, int max)
{
    char *save;
    int n = 0;
    snprintf(copy, cn, "%s", line);
    for (char *t = strtok_r(copy, " \t\r\n", &save); t && n < max;
         t = strtok_r(NULL, " \t\r\n", &save))
        tok[n++] = t;
    return n;
}

int uci_parse_info(const char *line, UciInfo *out)
{
    char copy[4096], *tok[UCI_MAX_TOKENS];
    int n = tokenize(line, copy, sizeof(copy), tok, UCI_MAX_TOKENS);
    if (n == 0 || strcmp(tok[0], "info") != 0) return 0;

    memset(out, 0, sizeof(*out));
    for (int i = 1; i < n; i++) {
        const char *k = tok[i];
        /* Everything after these is moves or free text. */
        if (strcmp(k, "pv") == 0 || strcmp(k, "string") == 0) break;
        if (i + 1 >= n) break;
        if      (strcmp(k, "depth") == 0) out->depth = atoi(tok[++i]);
        else if (strcmp(k, "nodes") == 0) out->nodes = atol(tok[++i]);
        else if (strcmp(k, "nps") == 0)   out->nps   = atol(tok[++i]);
        else if (strcmp(k, "score") == 0 && i + 2 < n) {
            if (strcmp(tok[i + 1], "cp") == 0) {
                out->has_score = 1;
                out->score_cp  = atoi(tok[i + 2]);
            } else if (strcmp(tok[i + 1], "mate") == 0) {
                out->has_score = 1;
                out->is_mate   = 1;
                out->mate_in   = atoi(tok[i + 2]);
            }
            i += 2;
        }
    }
    return 1;
}

int uci_parse_bestmove(const char *line, char *move, size_t n)
{
    char copy[256], *tok[4];
    int c = tokenize(line, copy, sizeof(copy), tok, 4);
    if (c == 0 || strcmp(tok[0], "bestmove") != 0) return 0;
    if (c < 2 || strcmp(tok[1], "(none)") == 0) move[0] = '\0';
    else snprintf(move, n, "%s", tok[1]);
    return 1;
}

int uci_parse_option(const char *line, char *name, size_t n, int *min, int *max)
{
    char copy[1024], *tok[UCI_MAX_TOKENS];
    int c = tokenize(line, copy, sizeof(copy), tok, UCI_MAX_TOKENS);
    if (c < 3 || strcmp(tok[0], "option") != 0 || strcmp(tok[1], "name") != 0) return 0;

    name[0] = '\0';
    *min = *max = 0;
    int i = 2;
    for (; i < c && strcmp(tok[i], "type") != 0; i++) {
        if (name[0]) strncat(name, " ", n - strlen(name) - 1);
        strncat(name, tok[i], n - strlen(name) - 1);
    }
    for (; i + 1 < c; i++) {
        if (strcmp(tok[i], "min") == 0)      *min = atoi(tok[++i]);
        else if (strcmp(tok[i], "max") == 0) *max = atoi(tok[++i]);
    }
    return 1;
}

void uci_position_command(const GameState *g, char *buf, size_t n)
{
    if (g->start_fen[0]) snprintf(buf, n, "position fen %s", g->start_fen);
    else                 snprintf(buf, n, "position startpos");
    if (g->move_count > g->log_start)
        strncat(buf, " moves", n - strlen(buf) - 1);

    for (int i = g->log_start; i < g->move_count; i++) {
        char m[8] = " ";
        move_to_str(g->move_made[i], m + 1);
        if (strlen(buf) + strlen(m) + 1 > n) break;
        strcat(buf, m);
    }
}

int uci_info_score(const UciInfo *info)
{
    if (!info->has_score) return 0;
    if (!info->is_mate)   return info->score_cp;
    int m = info->mate_in < 0 ? -info->mate_in : info->mate_in;
    return info->mate_in < 0 ? -(MATE_SCORE - m) : MATE_SCORE - m;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `make build/test_uci 2>&1 | grep -c warning; ./build/test_uci | tail -1`
Expected: `0`, then `All UCI tests passed.`

- [ ] **Step 6: Run the whole suite**

Run: `make -B test 2>&1 | tail -1`
Expected: `All suites passed.`

- [ ] **Step 7: Commit**

```bash
git add headers/game/uci.h src/game/uci.c tests/test_uci.c
git commit -m "feat: UCI line parsers and position command"
```

---

### Task 4: Fake engine, UCI driver and probe

**Files:**
- Create: `tests/fake_uci.c`
- Modify: `Makefile` (the `test:` rule)
- Modify: `headers/game/uci.h` (add the driver and probe declarations)
- Modify: `src/game/uci.c` (add the driver and probe)
- Test: `tests/test_uci.c` (add the driver tests)

**Interfaces:**
- Consumes: `EngineEntry` (Task 1); `OpponentOps`, `struct Opponent`, `opponent_*` (Task 2); the parsers (Task 3); `generate_moves`, `make_move`, `has_legal_moves`, `parse_move_str`, `parse_fen`.
- Produces:
  ```c
  typedef struct { char name[64]; char author[64]; int elo_supported, elo_min, elo_max; } UciProbe;
  #define UCI_HANDSHAKE_TIMEOUT_MS 10000
  #define UCI_PROBE_TIMEOUT_MS      5000
  #define UCI_STOP_TIMEOUT_MS       2000
  Opponent *opponent_uci(const EngineEntry *e);   /* NULL if out of memory */
  int  uci_probe(const char *path, UciProbe *out, char *err, size_t n);   /* 1 on uciok */
  ```
  Also the test binary `build/fake_uci`, with modes `normal`, `elo`, `slow`, `deaf`, `crash`, `illegal`, `mute` and `chatty`.

- [ ] **Step 1: Write the fake engine and build it**

Create `tests/fake_uci.c`:

```c
/* A scripted UCI engine for the tests. FAKE_UCI_MODE (or argv[1]) picks
 * how it behaves; FAKE_UCI_LOG, when set, gets every line it reads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine/board.h"
#include "engine/fen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "engine/movegen.h"
#include "utils/bitboard.h"

static const char *START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
static Position pos;

static int legal(Move m)
{
    Position t = pos;
    return make_move(&t, m);
}

static int first_legal(Move *out)
{
    MoveList ml;
    generate_moves(&pos, &ml);
    for (int i = 0; i < ml.count; i++)
        if (legal(ml.moves[i])) { *out = ml.moves[i]; return 1; }
    return 0;
}

static void play(const char *s)
{
    int from, to, promo;
    if (!parse_move_str(s, &from, &to, &promo)) return;
    MoveList ml;
    generate_moves(&pos, &ml);
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;
        if (promo && !(FLAGS(m) & promo)) continue;
        if (legal(m)) { make_move(&pos, m); return; }
    }
}

/* "startpos|fen <6 fields> [moves ...]" */
static void set_position(char *args)
{
    char *save, *t = strtok_r(args, " ", &save);
    if (!t) return;
    if (strcmp(t, "startpos") == 0) {
        parse_fen(START, &pos, NULL, NULL);
    } else if (strcmp(t, "fen") == 0) {
        char fen[128] = "";
        for (int i = 0; i < 6 && (t = strtok_r(NULL, " ", &save)); i++) {
            if (i) strncat(fen, " ", sizeof(fen) - strlen(fen) - 1);
            strncat(fen, t, sizeof(fen) - strlen(fen) - 1);
        }
        parse_fen(fen, &pos, NULL, NULL);
    }
    while ((t = strtok_r(NULL, " ", &save)))
        if (strcmp(t, "moves") != 0) play(t);
}

static void say(const char *s)
{
    printf("%s\n", s);
    fflush(stdout);
}

static void bestmove(void)
{
    Move m;
    char buf[8], line[32];
    if (!first_legal(&m)) { say("bestmove (none)"); return; }
    move_to_str(m, buf);
    snprintf(line, sizeof(line), "bestmove %s", buf);
    say(line);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : getenv("FAKE_UCI_MODE");
    if (!mode) mode = "normal";
    const char *logpath = getenv("FAKE_UCI_LOG");
    FILE *log = logpath ? fopen(logpath, "a") : NULL;

    init_attacks();
    parse_fen(START, &pos, NULL, NULL);

    static char line[16384];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (log) { fprintf(log, "%s\n", line); fflush(log); }
        if (strcmp(line, "quit") == 0) break;
        if (strcmp(mode, "mute") == 0) continue;

        if (strcmp(line, "uci") == 0) {
            say("id name Fake UCI");
            say("id author dchess tests");
            if (strcmp(mode, "elo") == 0) {
                say("option name UCI_LimitStrength type check default false");
                say("option name UCI_Elo type spin default 1320 min 1320 max 3190");
            }
            say("uciok");
        } else if (strcmp(line, "isready") == 0) {
            say("readyok");
        } else if (strncmp(line, "position ", 9) == 0) {
            set_position(line + 9);
        } else if (strncmp(line, "go", 2) == 0) {
            if (strcmp(mode, "crash") == 0) exit(3);
            if (strcmp(mode, "chatty") == 0) {
                static char big[10000];
                memset(big, 'x', sizeof(big) - 1);
                printf("info string %s\n", big);
                fflush(stdout);
            }
            say("info depth 1 score cp 12 nodes 20 nps 1000");
            if (strcmp(mode, "slow") == 0 || strcmp(mode, "deaf") == 0) continue;
            say("info depth 2 score cp 15 nodes 400 nps 2000");
            if (strcmp(mode, "illegal") == 0) say("bestmove e2e5");
            else bestmove();
        } else if (strcmp(line, "stop") == 0) {
            if (strcmp(mode, "slow") == 0) bestmove();
        }
    }
    if (log) fclose(log);
    return 0;
}
```

In `Makefile`, change the line `test: $(TEST_BIN)` to:

```make
test: build/fake_uci $(TEST_BIN)
```

The existing `build/%: tests/%.c $(CORE_SRC)` pattern rule already builds `build/fake_uci`, and `fake_uci.c` does not match `tests/test_*.c`, so it is not run as a suite.

Run: `make build/fake_uci 2>&1 | grep -c warning; printf 'uci\nisready\nposition startpos moves e2e4\ngo movetime 10\nquit\n' | ./build/fake_uci`
Expected: `0`, then `id name Fake UCI`, `id author dchess tests`, `uciok`, `readyok`, two `info` lines, and `bestmove` followed by a legal Black move such as `bestmove b8a6`.

- [ ] **Step 2: Write the failing driver tests**

In `tests/test_uci.c`, add these includes after `#include <string.h>`:

```c
#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
```

Add these helpers and test functions before `int main`:

```c
static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void nap(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

static int wait_result(Opponent *o, SearchResult *r, U64 *key, long budget_ms)
{
    long t0 = now_ms();
    while (now_ms() - t0 < budget_ms) {
        if (opponent_poll(o, r, key)) return 1;
        nap(5);
    }
    return 0;
}

static EngineEntry fake(int depth, int ms, int elo)
{
    EngineEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "Fake");
    snprintf(e.path, sizeof(e.path), "build/fake_uci");
    e.limit_depth = depth;
    e.limit_ms    = ms;
    e.elo         = elo;
    return e;
}

static void mode(const char *m) { setenv("FAKE_UCI_MODE", m, 1); }

static int no_children(void)
{
    return waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD;
}

static int is_legal(const GameState *g, Move m)
{
    Move found;
    return m && game_find_move(g, FROM(m), TO(m), FLAGS(m) & FLAG_PROMOTION, &found);
}

static int count(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)); p += strlen(needle)) n++;
    return n;
}

static void slurp(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t got = f ? fread(buf, 1, n - 1, f) : 0;
    buf[got] = '\0';
    if (f) fclose(f);
}

static void test_driver_normal(void)
{
    printf("== driver: a normal engine ==\n");
    mode("normal");
    GameState g;
    game_reset(&g);
    EngineEntry e = fake(0, 100, 0);
    SearchResult r;
    U64 key = 0;

    Opponent *o = opponent_uci(&e);
    check("the driver is created", o != NULL);
    check("poll before any start returns 0", !opponent_poll(o, &r, &key));
    check("start is accepted", opponent_start(o, &g));
    check("a second start while busy is refused", !opponent_start(o, &g));
    check("a result arrives", wait_result(o, &r, &key, 3000));
    check("it is a legal move", is_legal(&g, r.best_move));
    check("with the last info's depth, score and nodes",
          r.depth_reached == 2 && r.best_score == 15 && r.nodes == 400);
    check("and the game's key", key == game_hash(&g));
    check("no error", opponent_error(o) == NULL);

    game_play(&g, r.best_move);
    check("the same engine answers again",
          opponent_start(o, &g) && wait_result(o, &r, &key, 3000) && is_legal(&g, r.best_move));
    opponent_free(o);
    check("free leaves no child process", no_children());
}

static void test_driver_log(void)
{
    printf("== driver: what it sends ==\n");
    char logpath[] = "/tmp/dchess-uci-log-XXXXXX", buf[8192];
    int fd = mkstemp(logpath);
    close(fd);
    setenv("FAKE_UCI_LOG", logpath, 1);

    mode("elo");
    GameState g;
    game_reset(&g);
    play(&g, "e2e4");
    EngineEntry e = fake(0, 100, 1000);
    SearchResult r;
    U64 key;
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    game_play(&g, r.best_move);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    game_undo(&g);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    opponent_free(o);

    slurp(logpath, buf, sizeof(buf));
    check("limits strength when an Elo is set",
          strstr(buf, "setoption name UCI_LimitStrength value true") != NULL);
    check("with the Elo clamped to the engine's minimum",
          strstr(buf, "setoption name UCI_Elo value 1320") != NULL);
    check("sends the game with its moves", strstr(buf, "position startpos moves e2e4\n") != NULL);
    check("searches by time", strstr(buf, "go movetime 100") != NULL);
    check("ucinewgame first, and again after an undo", count(buf, "ucinewgame") == 2);
    check("quits on free", strstr(buf, "quit") != NULL);

    FILE *f = fopen(logpath, "w");
    fclose(f);
    mode("normal");
    game_reset(&g);
    e = fake(12, 0, 1500);
    o = opponent_uci(&e);
    opponent_start(o, &g);
    wait_result(o, &r, &key, 3000);
    opponent_free(o);
    slurp(logpath, buf, sizeof(buf));
    check("searches by depth", strstr(buf, "go depth 12") != NULL);
    check("no Elo options when the engine has no UCI_Elo", strstr(buf, "setoption") == NULL);

    unsetenv("FAKE_UCI_LOG");
    remove(logpath);
}

static void test_driver_stop_cancel(void)
{
    printf("== driver: stop and cancel ==\n");
    GameState g;
    game_reset(&g);
    EngineEntry e = fake(0, 60000, 0);
    SearchResult r;
    U64 key;

    mode("slow");
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    nap(300);
    long t0 = now_ms();
    opponent_stop(o);
    check("stop returns within 2s", now_ms() - t0 < 2000);
    check("and the next poll has its move", opponent_poll(o, &r, &key) && is_legal(&g, r.best_move));

    opponent_start(o, &g);
    nap(200);
    opponent_cancel(o);
    check("cancel leaves nothing to poll", !opponent_poll(o, &r, &key));
    check("and the driver starts again", opponent_start(o, &g));
    opponent_cancel(o);
    opponent_free(o);

    mode("deaf");
    o = opponent_uci(&e);
    opponent_start(o, &g);
    nap(300);
    t0 = now_ms();
    opponent_stop(o);
    long took = now_ms() - t0;
    check("an engine ignoring stop is killed after about 2s", took >= 1500 && took < 3000);
    check("and polls as no move", opponent_poll(o, &r, &key) && r.best_move == 0);
    check("with 'Fake: did not stop'",
          opponent_error(o) && strcmp(opponent_error(o), "Fake: did not stop") == 0);

    mode("normal");
    check("the next start relaunches it",
          opponent_start(o, &g) && wait_result(o, &r, &key, 3000) && is_legal(&g, r.best_move));
    check("and the error is cleared", opponent_error(o) == NULL);
    opponent_free(o);
    check("no child process is left", no_children());
}

static void expect_failure(const char *m, const char *path, const char *msg, long budget)
{
    GameState g;
    game_reset(&g);
    EngineEntry e = fake(0, 100, 0);
    snprintf(e.path, sizeof(e.path), "%s", path);
    SearchResult r;
    U64 key;
    char name[96];

    mode(m);
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    snprintf(name, sizeof(name), "%s: polls as no move", m);
    check(name, wait_result(o, &r, &key, budget) && r.best_move == 0);
    snprintf(name, sizeof(name), "%s: '%s'", m, msg);
    check(name, opponent_error(o) && strcmp(opponent_error(o), msg) == 0);
    opponent_free(o);
}

static void test_driver_failures(void)
{
    printf("== driver: failures ==\n");
    expect_failure("crash", "build/fake_uci", "Fake: engine exited", 3000);
    expect_failure("illegal", "build/fake_uci", "Fake: played illegal move e2e5", 3000);
    expect_failure("normal", "/nonexistent/dchess-engine",
                   "Fake: could not start /nonexistent/dchess-engine", 3000);
    long t0 = now_ms();
    expect_failure("mute", "build/fake_uci", "Fake: no reply from engine", 12000);
    check("mute waits out the 10s handshake", now_ms() - t0 >= 9000);
    check("no child process is left", no_children());
}

static void test_driver_edges(void)
{
    printf("== driver: edges ==\n");
    GameState g;
    SearchResult r;
    U64 key;
    EngineEntry e = fake(0, 100, 0);

    mode("normal");
    game_reset(&g);
    game_load_fen(&g, "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    Opponent *o = opponent_uci(&e);
    opponent_start(o, &g);
    check("a mated side gets no move and no error",
          wait_result(o, &r, &key, 3000) && r.best_move == 0 && opponent_error(o) == NULL);
    opponent_free(o);

    mode("chatty");
    game_reset(&g);
    o = opponent_uci(&e);
    opponent_start(o, &g);
    check("a 10 KB info line does not stop the search",
          wait_result(o, &r, &key, 3000) && is_legal(&g, r.best_move));
    opponent_free(o);
    check("no child process is left", no_children());
}

static void test_probe(void)
{
    printf("== probe ==\n");
    UciProbe p;
    char err[160];

    mode("normal");
    check("a probe reads the engine's name",
          uci_probe("build/fake_uci", &p, err, sizeof(err)) &&
          strcmp(p.name, "Fake UCI") == 0 && strcmp(p.author, "dchess tests") == 0);
    check("and sees no UCI_Elo", !p.elo_supported);
    mode("elo");
    check("and the Elo range when offered",
          uci_probe("build/fake_uci", &p, err, sizeof(err)) &&
          p.elo_supported && p.elo_min == 1320 && p.elo_max == 3190);
    check("a missing file is an error",
          !uci_probe("/nonexistent/dchess-engine", &p, err, sizeof(err)) &&
          strcmp(err, "could not start /nonexistent/dchess-engine") == 0);
    mode("mute");
    long t0 = now_ms();
    check("an engine that never answers times out",
          !uci_probe("build/fake_uci", &p, err, sizeof(err)) &&
          strcmp(err, "no reply from engine") == 0);
    check("after about 5s", now_ms() - t0 >= 4500 && now_ms() - t0 < 7000);
    check("no child process is left", no_children());
}

static void test_probe_not_an_engine(void)
{
    printf("== probe: not an engine ==\n");
    UciProbe p;
    char err[160];
    check("/bin/true exits: could not start",
          !uci_probe("/bin/true", &p, err, sizeof(err)) &&
          strcmp(err, "could not start /bin/true") == 0);
    check("/bin/cat echoes but never says uciok",
          !uci_probe("/bin/cat", &p, err, sizeof(err)) &&
          strcmp(err, "no reply from engine") == 0);
    check("no child process is left", no_children());
}
```

Add the includes `#include "game/opponent.h"` and `#include "utils/engines.h"` after `#include "game/uci.h"`. In `main`, after `test_position();`, add:

```c
    test_driver_normal();
    test_driver_log();
    test_driver_stop_cancel();
    test_driver_failures();
    test_driver_edges();
    test_probe();
    test_probe_not_an_engine();
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `make build/fake_uci build/test_uci 2>&1 | grep -m1 error`
Expected: an implicit declaration of `opponent_uci`, or an unknown type `UciProbe`.

- [ ] **Step 4: Declare the driver and probe**

In `headers/game/uci.h`, add `#include "game/opponent.h"` and `#include "utils/engines.h"` after `#include "game/game.h"`. Then add, before `#endif`:

```c
#define UCI_HANDSHAKE_TIMEOUT_MS 10000
#define UCI_PROBE_TIMEOUT_MS      5000
#define UCI_STOP_TIMEOUT_MS       2000

typedef struct {
    char name[64];
    char author[64];
    int  elo_supported, elo_min, elo_max;
} UciProbe;

/* The engine starts on the first opponent_start(). The entry is copied. */
Opponent *opponent_uci(const EngineEntry *e);

/* Starts the engine, waits for "uciok" and quits it again. Blocks for at
 * most UCI_PROBE_TIMEOUT_MS. */
int  uci_probe(const char *path, UciProbe *out, char *err, size_t n);
```

- [ ] **Step 5: Write the driver and probe**

In `src/game/uci.c`, replace the include block with:

```c
#include "game/uci.h"
#include "game/opponent_impl.h"
#include "engine/make.h"
#include "engine/move.h"
#include "engine/movegen.h"
#include "utils/constants.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
```

Then append to the end of `src/game/uci.c`:

```c
static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void nap(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* A write to an engine that just died must be an error, not a crash. */
static void ignore_sigpipe(void)
{
    static int done;
    if (!done) {
        signal(SIGPIPE, SIG_IGN);
        done = 1;
    }
}

/* The engine's stderr goes nowhere, so it cannot draw over the screen. */
static int spawn(const char *path, pid_t *pid, int *to_fd, int *from_fd)
{
    int in[2], out[2];
    if (pipe(in) != 0) return 0;
    if (pipe(out) != 0) {
        close(in[0]);
        close(in[1]);
        return 0;
    }
    int fds[4] = { in[0], in[1], out[0], out[1] };
    for (int i = 0; i < 4; i++) fcntl(fds[i], F_SETFD, FD_CLOEXEC);

    pid_t p = fork();
    if (p < 0) {
        for (int i = 0; i < 4; i++) close(fds[i]);
        return 0;
    }
    if (p == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) {
            dup2(null, 2);
            if (null > 2) close(null);
        }
        char *argv[] = { (char *)path, NULL };
        execvp(path, argv);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    fcntl(out[0], F_SETFL, fcntl(out[0], F_GETFL) | O_NONBLOCK);
    *pid = p;
    *to_fd = in[1];
    *from_fd = out[0];
    return 1;
}

static void reap(pid_t pid, int grace_ms)
{
    long until = now_ms() + grace_ms;
    while (now_ms() < until) {
        if (waitpid(pid, NULL, WNOHANG) == pid) return;
        nap(10);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

typedef enum { U_OFF, U_WAIT_UCIOK, U_WAIT_READY, U_IDLE, U_SEARCHING } UciState;

typedef struct {
    Opponent     base;
    EngineEntry  entry;

    pid_t        pid;          /* 0 while no engine is running */
    int          to_fd, from_fd;
    char         line[4096];
    int          len;
    int          skipping;     /* dropping the rest of an over-long line */

    UciState     state;
    long         deadline_ms;
    int          elo_supported, elo_min, elo_max;

    int          busy;         /* from start until the poll that returns */
    int          pending;      /* a search waiting for the handshake */
    int          done;         /* a result is ready for poll */
    SearchResult result;
    U64          key;
    Position     pos;          /* the position being searched */
    char         command[UCI_COMMAND_MAX];
    int          fresh;        /* send ucinewgame first */
    int          searched;
    char         last_fen[FEN_BUFSIZE];
    int          last_count;
    UciInfo      info;
    long         started_ms;
    char         error[400];
} Uci;

static void shut(Uci *u, int polite)
{
    if (!u->pid) return;
    if (polite && write(u->to_fd, "quit\n", 5) < 0) {}
    close(u->to_fd);
    reap(u->pid, polite ? 500 : 0);
    close(u->from_fd);
    u->pid = 0;
    u->state = U_OFF;
    u->len = 0;
    u->skipping = 0;
}

static void fail(Uci *u, const char *what)
{
    snprintf(u->error, sizeof(u->error), "%s: %s", u->entry.name, what);
    shut(u, 0);
    if (u->busy) {
        memset(&u->result, 0, sizeof(u->result));
        u->pending = 0;
        u->done = 1;
    }
}

static void gone(Uci *u)
{
    char what[320];
    if (u->state == U_WAIT_UCIOK || u->state == U_WAIT_READY)
        snprintf(what, sizeof(what), "could not start %s", u->entry.path);
    else
        snprintf(what, sizeof(what), "engine exited");
    fail(u, what);
}

static int say(Uci *u, const char *s)
{
    char buf[UCI_COMMAND_MAX + 2];
    size_t len = strlen(s), off = 0;
    if (len > UCI_COMMAND_MAX) len = UCI_COMMAND_MAX;
    memcpy(buf, s, len);
    buf[len++] = '\n';
    while (off < len) {
        ssize_t w = write(u->to_fd, buf + off, len - off);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) {
            gone(u);
            return 0;
        }
        off += (size_t)w;
    }
    return 1;
}

static void send_search(Uci *u)
{
    char go[48];
    if (u->fresh && !say(u, "ucinewgame")) return;
    if (!say(u, u->command)) return;
    if (u->entry.limit_depth) snprintf(go, sizeof(go), "go depth %d", u->entry.limit_depth);
    else                      snprintf(go, sizeof(go), "go movetime %d", u->entry.limit_ms);
    if (!say(u, go)) return;
    u->state = U_SEARCHING;
    u->pending = 0;
    u->searched = 1;
    u->started_ms = now_ms();
}

static void after_uciok(Uci *u)
{
    if (u->entry.elo && u->elo_supported) {
        int elo = u->entry.elo;
        if (elo < u->elo_min) elo = u->elo_min;
        if (u->elo_max && elo > u->elo_max) elo = u->elo_max;
        char opt[64];
        snprintf(opt, sizeof(opt), "setoption name UCI_Elo value %d", elo);
        if (!say(u, "setoption name UCI_LimitStrength value true")) return;
        if (!say(u, opt)) return;
    }
    if (!say(u, "isready")) return;
    u->state = U_WAIT_READY;
    u->deadline_ms = now_ms() + UCI_HANDSHAKE_TIMEOUT_MS;
}

static int legal_move(const Position *pos, const char *s, Move *out)
{
    int from, to, promo;
    if (!parse_move_str(s, &from, &to, &promo)) return 0;
    MoveList ml;
    generate_moves(pos, &ml);
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;
        if ((FLAGS(m) & FLAG_PROMOTION) && !(FLAGS(m) & (promo ? promo : FLAG_PROMO_Q)))
            continue;
        Position t = *pos;
        if (make_move(&t, m)) {
            *out = m;
            return 1;
        }
    }
    return 0;
}

static void finish(Uci *u, const char *move)
{
    Move m = 0;
    u->state = U_IDLE;
    /* An empty move is only honest when there is nothing to play. */
    if ((move[0] || has_legal_moves(&u->pos)) && !legal_move(&u->pos, move, &m)) {
        char what[64];
        snprintf(what, sizeof(what), "played illegal move %s", move[0] ? move : "(none)");
        fail(u, what);
        return;
    }
    memset(&u->result, 0, sizeof(u->result));
    u->result.best_move     = m;
    u->result.best_score    = uci_info_score(&u->info);
    u->result.depth_reached = u->info.depth;
    u->result.nodes         = u->info.nodes;
    u->result.elapsed_ms    = now_ms() - u->started_ms;
    u->done = 1;
}

static void merge(UciInfo *into, const UciInfo *from)
{
    if (from->depth) into->depth = from->depth;
    if (from->nodes) into->nodes = from->nodes;
    if (from->nps)   into->nps   = from->nps;
    if (from->has_score) {
        into->has_score = 1;
        into->score_cp  = from->score_cp;
        into->is_mate   = from->is_mate;
        into->mate_in   = from->mate_in;
    }
}

static void on_line(Uci *u, const char *line)
{
    char name[64], move[16];
    int lo, hi;
    UciInfo info;

    switch (u->state) {
    case U_WAIT_UCIOK:
        if (uci_parse_option(line, name, sizeof(name), &lo, &hi)) {
            if (strcmp(name, "UCI_Elo") == 0) {
                u->elo_supported = 1;
                u->elo_min = lo;
                u->elo_max = hi;
            }
        } else if (strcmp(line, "uciok") == 0) {
            after_uciok(u);
        }
        break;
    case U_WAIT_READY:
        if (strcmp(line, "readyok") == 0) {
            u->state = U_IDLE;
            if (u->pending) send_search(u);
        }
        break;
    case U_SEARCHING:
        if (uci_parse_info(line, &info))
            merge(&u->info, &info);
        else if (uci_parse_bestmove(line, move, sizeof(move)))
            finish(u, move);
        break;
    default:
        break;
    }
}

static void feed(Uci *u, const char *p, ssize_t n)
{
    for (ssize_t i = 0; i < n && u->pid; i++) {
        if (p[i] == '\n') {
            if (!u->skipping) {
                while (u->len && (u->line[u->len - 1] == '\r' || u->line[u->len - 1] == ' '))
                    u->len--;
                u->line[u->len] = '\0';
                on_line(u, u->line);
            }
            u->len = 0;
            u->skipping = 0;
        } else if (!u->skipping) {
            if (u->len < (int)sizeof(u->line) - 1) {
                u->line[u->len++] = p[i];
            } else {
                /* Too long: act on what fits, drop the rest. */
                u->line[u->len] = '\0';
                on_line(u, u->line);
                u->len = 0;
                u->skipping = 1;
            }
        }
    }
}

static void pump(Uci *u)
{
    char chunk[1024];
    while (u->pid) {
        ssize_t r = read(u->from_fd, chunk, sizeof(chunk));
        if (r > 0) { feed(u, chunk, r); continue; }
        if (r == 0) { gone(u); return; }
        if (errno != EINTR) return;
    }
}

static void check_deadline(Uci *u)
{
    if ((u->state == U_WAIT_UCIOK || u->state == U_WAIT_READY) && now_ms() > u->deadline_ms)
        fail(u, "no reply from engine");
}

static int uci_start(Opponent *o, const GameState *g)
{
    Uci *u = (Uci *)o;
    if (u->busy) return 0;

    u->error[0] = '\0';
    u->busy = 1;
    u->done = 0;
    u->pending = 1;
    memset(&u->info, 0, sizeof(u->info));
    u->pos = g->pos;
    u->key = game_hash(g);
    uci_position_command(g, u->command, sizeof(u->command));
    u->fresh = !u->searched || g->move_count < u->last_count ||
               strcmp(g->start_fen, u->last_fen) != 0;
    u->last_count = g->move_count;
    snprintf(u->last_fen, sizeof(u->last_fen), "%s", g->start_fen);

    if (!u->pid) {
        u->searched = 0;
        u->fresh = 1;
        u->elo_supported = 0;
        if (!spawn(u->entry.path, &u->pid, &u->to_fd, &u->from_fd)) {
            char what[320];
            snprintf(what, sizeof(what), "could not start %s", u->entry.path);
            fail(u, what);
            return 1;
        }
        u->state = U_WAIT_UCIOK;
        u->deadline_ms = now_ms() + UCI_HANDSHAKE_TIMEOUT_MS;
        say(u, "uci");
        return 1;
    }
    if (u->state == U_IDLE) send_search(u);
    return 1;
}

static int uci_poll(Opponent *o, SearchResult *out, U64 *key)
{
    Uci *u = (Uci *)o;
    if (!u->busy) return 0;
    pump(u);
    check_deadline(u);
    if (!u->done) return 0;
    if (out) *out = u->result;
    if (key) *key = u->key;
    u->busy = 0;
    u->done = 0;
    return 1;
}

static void uci_stop(Opponent *o)
{
    Uci *u = (Uci *)o;
    if (!u->busy || u->done) return;
    if (u->state != U_SEARCHING) {
        /* The search never began: it stops with no move. */
        u->pending = 0;
        memset(&u->result, 0, sizeof(u->result));
        u->done = 1;
        return;
    }
    if (!say(u, "stop")) return;
    long until = now_ms() + UCI_STOP_TIMEOUT_MS;
    while (u->pid && u->state == U_SEARCHING && now_ms() < until) {
        struct pollfd pf = { u->from_fd, POLLIN, 0 };
        poll(&pf, 1, 20);
        pump(u);
    }
    if (u->state == U_SEARCHING) fail(u, "did not stop");
}

static void uci_cancel(Opponent *o)
{
    Uci *u = (Uci *)o;
    if (!u->busy) return;
    uci_stop(o);
    u->busy = 0;
    u->done = 0;
}

static void uci_destroy(Opponent *o)
{
    Uci *u = (Uci *)o;
    shut(u, 1);
    free(u);
}

static const char *uci_error(const Opponent *o)
{
    const Uci *u = (const Uci *)o;
    return u->error[0] ? u->error : NULL;
}

static const OpponentOps uci_ops = {
    uci_start, uci_poll, uci_stop, uci_cancel, uci_destroy, uci_error,
};

Opponent *opponent_uci(const EngineEntry *e)
{
    Uci *u = calloc(1, sizeof(*u));
    if (!u) return NULL;
    ignore_sigpipe();
    u->base.ops = &uci_ops;
    u->entry = *e;
    return &u->base;
}

static void probe_line(const char *line, UciProbe *p, int *ok)
{
    char name[64];
    int lo, hi;
    if (strncmp(line, "id name ", 8) == 0)
        snprintf(p->name, sizeof(p->name), "%s", line + 8);
    else if (strncmp(line, "id author ", 10) == 0)
        snprintf(p->author, sizeof(p->author), "%s", line + 10);
    else if (uci_parse_option(line, name, sizeof(name), &lo, &hi) && strcmp(name, "UCI_Elo") == 0) {
        p->elo_supported = 1;
        p->elo_min = lo;
        p->elo_max = hi;
    } else if (strcmp(line, "uciok") == 0)
        *ok = 1;
}

int uci_probe(const char *path, UciProbe *out, char *err, size_t n)
{
    pid_t pid;
    int to, from, ok = 0, eof = 0, len = 0;
    char line[4096], chunk[512];

    memset(out, 0, sizeof(*out));
    ignore_sigpipe();
    if (!spawn(path, &pid, &to, &from)) {
        snprintf(err, n, "could not start %s", path);
        return 0;
    }

    if (write(to, "uci\n", 4) != 4) eof = 1;
    long until = now_ms() + UCI_PROBE_TIMEOUT_MS;
    while (!ok && !eof && now_ms() < until) {
        struct pollfd pf = { from, POLLIN, 0 };
        poll(&pf, 1, 20);
        ssize_t r = -1;
        while (!ok && (r = read(from, chunk, sizeof(chunk))) > 0) {
            for (ssize_t i = 0; i < r && !ok; i++) {
                if (chunk[i] != '\n') {
                    if (len < (int)sizeof(line) - 1) line[len++] = chunk[i];
                    continue;
                }
                while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) len--;
                line[len] = '\0';
                len = 0;
                probe_line(line, out, &ok);
            }
        }
        if (!ok && r == 0) eof = 1;
    }

    if (write(to, "quit\n", 5) < 0) {}
    close(to);
    reap(pid, 500);
    close(from);

    if (ok) return 1;
    if (eof) snprintf(err, n, "could not start %s", path);
    else     snprintf(err, n, "no reply from engine");
    return 0;
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `make build/fake_uci build/test_uci 2>&1 | grep -c warning; ./build/test_uci | tail -3`
Expected: `0`, then `All UCI tests passed.` The run takes about 20 s, mostly the three timeouts.

- [ ] **Step 7: Run the whole suite**

Run: `make -B test 2>&1 | tail -1`
Expected: `All suites passed.`

- [ ] **Step 8: Commit**

```bash
git add Makefile tests/fake_uci.c headers/game/uci.h src/game/uci.c tests/test_uci.c
git commit -m "feat: drive UCI engines over non-blocking pipes"
```

---

### Task 5: The UCI player kind

**Files:**
- Modify: `headers/game/players.h`
- Modify: `src/game/players.c`
- Modify: `src/tui/commands.c` (the `players_apply_command` call)
- Test: `tests/test_players.c`

**Interfaces:**
- Consumes: `EngineList`, `engines_find`, `ENGINE_NAME_MAX` (Task 1).
- Produces:
  ```c
  typedef enum { PLAYER_HUMAN, PLAYER_BUILTIN, PLAYER_UCI } PlayerKind;
  /* Player gains: char engine[ENGINE_NAME_MAX + 1]; */
  Player player_uci(const char *name);
  int players_apply_command(Player p[2], const char *cmd, char *err, size_t n,
                            const EngineList *engines);   /* engines may be NULL */
  ```

- [ ] **Step 1: Write the failing test**

In `tests/test_players.c`, add `, NULL` as a last argument to every existing `players_apply_command(...)` call (there are 12). Add this function before `int main`:

```c
static void test_uci_players(void)
{
    printf("== UCI players ==\n");
    char buf[64], err[128];
    int side, level;

    Player u = player_uci("Stockfish 1500");
    check("a UCI player keeps its entry name",
          u.kind == PLAYER_UCI && strcmp(u.engine, "Stockfish 1500") == 0);
    Player hu[2] = { H(), u };
    check("it is automated", players_automated(hu, BLACK));
    player_label(&u, buf, sizeof(buf));
    check("its label is the entry name", strcmp(buf, "Stockfish 1500") == 0);
    players_pgn_name(hu, BLACK, buf, sizeof(buf));
    check("and so is its PGN name", strcmp(buf, "Stockfish 1500") == 0);
    players_matchup(hu, buf, sizeof(buf));
    check("matchup: 'You vs Stockfish 1500'", strcmp(buf, "You vs Stockfish 1500") == 0);
    check("games against it are not recorded yet", !players_stats_entry(hu, &side, &level));

    EngineList reg;
    memset(&reg, 0, sizeof(reg));
    reg.count = 1;
    snprintf(reg.e[0].name, sizeof(reg.e[0].name), "Stockfish 1500");
    snprintf(reg.e[0].path, sizeof(reg.e[0].path), "stockfish");

    Player p[2] = { H(), E(DIFF_MEDIUM) };
    check("'white engine Stockfish 1500' picks the entry",
          players_apply_command(p, "white engine Stockfish 1500", err, sizeof(err), &reg) == 1 &&
          p[WHITE].kind == PLAYER_UCI && strcmp(p[WHITE].engine, "Stockfish 1500") == 0);
    check("a level still means the built-in engine",
          players_apply_command(p, "white engine hard", err, sizeof(err), &reg) == 1 &&
          p[WHITE].kind == PLAYER_BUILTIN && p[WHITE].level == DIFF_HARD);
    check("an unknown name is refused, naming it",
          players_apply_command(p, "black engine Komodo", err, sizeof(err), &reg) == -1 &&
          strstr(err, "Komodo") != NULL);
    check("without a registry no name resolves",
          players_apply_command(p, "black engine Stockfish 1500", err, sizeof(err), NULL) == -1);
    check("trailing spaces are ignored",
          players_apply_command(p, "black human  ", err, sizeof(err), NULL) == 1 &&
          p[BLACK].kind == PLAYER_HUMAN);
    check("a longer word is not a colour",
          players_apply_command(p, "whitehorse", err, sizeof(err), NULL) == 0);
}
```

In `main`, add `test_uci_players();` after `test_labels();`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_players 2>&1 | grep -m1 error`
Expected: `too many arguments to function 'players_apply_command'`, or an undeclared `player_uci`.

- [ ] **Step 3: Update the header**

In `headers/game/players.h`:
- Add `#include "utils/engines.h"` after `#include <stddef.h>`.
- Change the enum to `typedef enum { PLAYER_HUMAN, PLAYER_BUILTIN, PLAYER_UCI } PlayerKind;`.
- Change the struct comment to `/* level, depth and time_ms are for the built-in engine; engine names a registry entry. */`.
- Add this field after `int time_ms;`:

```c
    char       engine[ENGINE_NAME_MAX + 1];
```
- Add `Player      player_uci(const char *name);` after `player_builtin`.
- Replace the `players_apply_command` declaration with:

```c
/* 1 applied, 0 not a player command, -1 invalid with a message in err.
 * `engines` resolves names after "engine"; NULL resolves none. */
int  players_apply_command(Player p[2], const char *cmd, char *err, size_t n,
                           const EngineList *engines);
```

- [ ] **Step 4: Update the implementation**

In `src/game/players.c`:

1. In `player_human` and `player_builtin`, add `""` as the last initialiser, e.g. `Player p = { PLAYER_HUMAN, DIFF_MEDIUM, 0, 0, "" };` and `Player p = { PLAYER_BUILTIN, level, cli_depth_for_difficulty(level), cli_time_limit_for_difficulty(level), "" };`.

2. After `player_builtin`, add:

```c
Player player_uci(const char *name)
{
    Player p = { PLAYER_UCI, DIFF_MEDIUM, 0, 0, "" };
    snprintf(p.engine, sizeof(p.engine), "%s", name);
    return p;
}
```

3. Replace the whole of `players_apply_command` with:

```c
int players_apply_command(Player p[2], const char *cmd, char *err, size_t n,
                          const EngineList *engines)
{
    char line[128];
    snprintf(line, sizeof(line), "%s", cmd);
    size_t len = strlen(line);
    while (len && line[len - 1] == ' ') line[--len] = '\0';

    if (strcmp(line, "swap") == 0) {
        Player t = p[WHITE];
        p[WHITE] = p[BLACK];
        p[BLACK] = t;
        return 1;
    }

    int side = strncmp(line, "white", 5) == 0 ? WHITE :
               strncmp(line, "black", 5) == 0 ? BLACK : -1;
    if (side < 0 || (line[5] != '\0' && line[5] != ' ')) return 0;

    const char *usage =
        "Use: white|black human, or white|black engine [easy|medium|hard|name]";
    const char *rest = line + 5;
    while (*rest == ' ') rest++;

    if (strcmp(rest, "human") == 0) {
        p[side] = player_human();
        return 1;
    }
    if (strncmp(rest, "human ", 6) == 0) {
        snprintf(err, n, "A human has no level. %s", usage);
        return -1;
    }
    if (strcmp(rest, "engine") == 0) {
        p[side] = player_builtin(DIFF_MEDIUM);
        return 1;
    }
    if (strncmp(rest, "engine ", 7) == 0) {
        const char *arg = rest + 7;
        while (*arg == ' ') arg++;
        int lv = players_level_from_name(arg);
        if (lv >= 0) {
            p[side] = player_builtin(lv);
            return 1;
        }
        if (engines && engines_find(engines, arg)) {
            p[side] = player_uci(arg);
            return 1;
        }
        snprintf(err, n, "Unknown engine '%s'. Use easy, medium, hard or a name from 'engines'", arg);
        return -1;
    }
    snprintf(err, n, "%s", usage);
    return -1;
}
```

4. In `player_label`, replace the body with:

```c
    if (p->kind == PLAYER_HUMAN)
        snprintf(buf, n, "You");
    else if (p->kind == PLAYER_UCI)
        snprintf(buf, n, "%s", p->engine);
    else
        snprintf(buf, n, "dchess %s", players_level_name(p->level));
```

5. In `players_pgn_name`, add this branch first:

```c
    if (p[side].kind == PLAYER_UCI)
        snprintf(buf, n, "%s", p[side].engine);
    else if (p[side].kind == PLAYER_BUILTIN)
```
Then turn the existing `if (p[side].kind == PLAYER_BUILTIN)` line into the `else if` above; the rest is unchanged.

6. In `players_matchup` and `players_describe`, change `char w[32], b[32];` to `char w[48], b[48];`.

In `src/tui/commands.c`, change `players_apply_command(state->players, cmd, err, sizeof(err))` to `players_apply_command(state->players, cmd, err, sizeof(err), NULL)`. Task 7 passes the registry.

- [ ] **Step 5: Run the tests and the build**

Run: `make build/test_players 2>&1 | grep -c warning; ./build/test_players | tail -1; make -B dchess 2>&1 | grep -c warning; make -B test 2>&1 | tail -1`
Expected: `0`, `All player tests passed.`, `0`, `All suites passed.`

- [ ] **Step 6: Commit**

```bash
git add headers/game/players.h src/game/players.c src/tui/commands.c tests/test_players.c
git commit -m "feat: UCI engines as a kind of player"
```

---

### Task 6: Engines on the command line

**Files:**
- Modify: `headers/utils/cli.h`
- Modify: `src/utils/cli.c`
- Modify: `src/main.c`
- Modify: `README.md`
- Test: `tests/test_cli.c`

**Interfaces:**
- Consumes: `engines_load`, `engines_find`, `engines_path`, `engine_strength_label` (Task 1); `player_uci` (Task 5).
- Produces: `CliArgs.list_engines` (int), and `void cli_list_engines(void)`, which prints the registry and exits.

- [ ] **Step 1: Write the failing test**

In `tests/test_cli.c`, add after `#include "utils/constants.h"`:

```c
#include "utils/engines.h"
#include <stdlib.h>
#include <unistd.h>
```

Add before `int main`:

```c
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
          a.players[WHITE].kind == PLAYER_UCI && strcmp(a.players[WHITE].engine, "Fake") == 0 &&
          engine(&a.players[BLACK], DIFF_HARD));

    char *av[] = { "dchess", "--black", "Stockfish 1500" };
    check("a name with spaces, as one argument",
          cli_parse(3, av, &a) == 0 && a.players[BLACK].kind == PLAYER_UCI &&
          strcmp(a.players[BLACK].engine, "Stockfish 1500") == 0);

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
```

In `main`, add `test_engines();` before the final `if (failures)`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `make build/test_cli 2>&1 | grep -m1 error`
Expected: `'CliArgs' has no member named 'list_engines'`.

- [ ] **Step 3: Add the field and the declaration**

In `headers/utils/cli.h`, after `int show_stats;     /* --stats flag    */`, add:

```c
    int list_engines;   /* --engines flag */
```

After `void cli_version(void);`, add:

```c
/* Prints the registered UCI engines and exits. */
void cli_list_engines(void);
```

- [ ] **Step 4: Parse names and `--engines`**

In `src/utils/cli.c`:

1. Add `#include "utils/engines.h"` after `#include "utils/theme.h"`.

2. Add before `/* Parser ───`:

```c
/* Only a value that is not a built-in word reads the registry, so a
 * broken engines.conf never stops an ordinary start. */
static int known_engine(const char *name, char *err, size_t n)
{
    EngineList l;
    engines_load(&l);
    if (engines_find(&l, name)) return 1;
    if (!l.count) {
        snprintf(err, n, "Unknown player '%s'. Use: human | easy | medium | hard "
                         "(no engines registered)", name);
        return 0;
    }
    char names[160] = "";
    for (int i = 0; i < l.count; i++) {
        if (i) strncat(names, ", ", sizeof(names) - strlen(names) - 1);
        strncat(names, l.e[i].name, sizeof(names) - strlen(names) - 1);
    }
    snprintf(err, n, "Unknown player '%s'. Use: human | easy | medium | hard, "
                     "or an engine: %s", name, names);
    return 0;
}

void cli_list_engines(void)
{
    EngineList l;
    char path[512];
    int have = engines_path(path, sizeof(path));
    engines_load(&l);
    if (!l.count) {
        printf("No engines registered%s%s.\n", have ? " in " : "", have ? path : "");
        printf("Add one from the start menu with the e key.\n");
        exit(0);
    }
    printf("  %-24s %-18s %s\n", "NAME", "STRENGTH", "PATH");
    for (int i = 0; i < l.count; i++) {
        char s[48];
        engine_strength_label(&l.e[i], s, sizeof(s));
        printf("  %-24s %-18s %s\n", l.e[i].name, s, l.e[i].path);
    }
    exit(0);
}
```

3. In `cli_parse`, add `args->list_engines = 0;` after `args->show_stats   = 0;`.

4. After the `--stats` block, add:

```c
        /* --engines ─────────────────────────────────────────────────── */
        if (strcmp(a, "--engines") == 0) {
            args->list_engines = 1;
            return 0;
        }
```

5. In the `--white` / `--black` branch, replace:

```c
            } else if (lv >= 0) {
                chosen[side] = player_builtin(lv);
            } else {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Unknown player '%s'. Use: human | easy | medium | hard", val);
                args->error = 1;
                return -1;
            }
```
with:
```c
            } else if (lv >= 0) {
                chosen[side] = player_builtin(lv);
            } else if (known_engine(val, args->error_msg, sizeof(args->error_msg))) {
                chosen[side] = player_uci(val);
            } else {
                args->error = 1;
                return -1;
            }
```

6. In the `cli_help` text:
   - Change both `"    --white <human|easy|medium|hard>\n"` and the matching `--black` line so the placeholder reads `<human|easy|medium|hard|engine>`.
   - After the line `"            dchess --white hard --black easy\n"`, add:

```c
        "          An engine is a name from engines.conf, e.g.\n"
        "            dchess --white \"Stockfish 1500\" --black hard\n"
        "\n"
        "    --engines\n"
        "          List the registered UCI engines and exit.\n"
```
   - In the in-game commands, change `"    white|black engine [easy|medium|hard]\n"` to `"    white|black engine [easy|medium|hard|name]\n"`. After the `swap` line, add:

```c
        "    engines     List the registered UCI engines\n"
```

- [ ] **Step 5: Handle `--engines` in `main`**

In `src/main.c`, after `if (args.show_version) cli_version();  /* exits */`, add:

```c
    if (args.list_engines) cli_list_engines();   /* exits */
```

- [ ] **Step 6: Update the README**

In `README.md`'s CLI block, replace the two lines `  --white <human|easy|medium|hard>` and `  --black <human|easy|medium|hard>` with:

```
  --white <human|easy|medium|hard|engine>
  --black <human|easy|medium|hard|engine>
```
After the `Choose who plays a side; overrides -c, -d and -2` line, add:

```
  --engines                       List the registered UCI engines and exit
```
After the `Watch the engine play itself` example, add:

```
  dchess --white "Stockfish 1500" --black hard
                                  A registered UCI engine against dchess
```
In the command-mode block, change `white|black engine [easy|medium|hard]` to `white|black engine [easy|medium|hard|name]`, and after the `swap` line add:

```
engines     list the registered UCI engines
```

- [ ] **Step 7: Run the tests and the build**

Run: `make -B test 2>&1 | tail -1; ./build/test_cli | tail -1; make -B dchess 2>&1 | grep -c warning; XDG_CONFIG_HOME=/nonexistent ./dchess --engines`
Expected: `All suites passed.`, `All CLI tests passed.`, `0`, then `No engines registered in /nonexistent/dchess/engines.conf.` followed by the hint line.

- [ ] **Step 8: Commit**

```bash
git add headers/utils/cli.h src/utils/cli.c src/main.c README.md tests/test_cli.c
git commit -m "feat(cli): pick registered engines with --white/--black and list them"
```

---

### Task 7: UCI engines in the game

**Files:**
- Modify: `headers/tui/tui.h`
- Modify: `src/tui/tui.c` (`tui_init`)
- Modify: `src/tui/commands.c`
- Modify: `src/tui/panels.c` (`draw_engine_panel`)

**Interfaces:**
- Consumes: `opponent_uci`, `opponent_error` (Tasks 2 and 4); `engines_load`, `engines_find` (Task 1); `players_apply_command(..., engines)` (Task 5).
- Produces: the `TUIState` fields `EngineList engines` and `char engine_error[400]`. `thinking_by` and `last_search_by` widen to `[48]`.

- [ ] **Step 1: Widen and extend the state**

In `headers/tui/tui.h`:
- Change `char     last_search_by[32];` to `char     last_search_by[48];`.
- Change `char      thinking_by[32];` to `char      thinking_by[48];`.
- After `long      last_move_ms;   /* monotonic; when an engine last moved */`, add:

```c
    EngineList engines;       /* engines.conf, as of the last load */
    char      engine_error[400];   /* the last engine failure, until the next search */
```

In `src/tui/tui.c`, `tui_init`, after `stats_load(&state->stats);`, add:

```c
    engines_load(&state->engines);
```

- [ ] **Step 2: Build UCI drivers and report failures**

In `src/tui/commands.c`:

1. Add `#include "game/uci.h"` after `#include "game/pgn.h"`.

2. In `same_player`, change the return to:

```c
    return a->kind == b->kind && a->level == b->level &&
           a->depth == b->depth && a->time_ms == b->time_ms &&
           strcmp(a->engine, b->engine) == 0;
```

3. In `attach`, replace:

```c
    if (p->kind == PLAYER_BUILTIN)
        state->drivers[side] = opponent_builtin(p->depth, p->time_ms);
```
with:
```c
    if (p->kind == PLAYER_BUILTIN) {
        state->drivers[side] = opponent_builtin(p->depth, p->time_ms);
    } else if (p->kind == PLAYER_UCI) {
        const EngineEntry *e = engines_find(&state->engines, p->engine);
        if (e) state->drivers[side] = opponent_uci(e);
    }
```

4. Replace `start_thinking` with:

```c
static void start_thinking(TUIState *state, Opponent *o, const char *by)
{
    if (!o) {
        /* Pausing stops the next tick from trying again at once. */
        state->paused = 1;
        snprintf(state->status, sizeof(state->status),
                 "Could not start %s — paused", by);
        return;
    }
    if (!opponent_start(o, &state->game)) return;

    state->engine_error[0] = '\0';
    state->thinking = o;
    snprintf(state->thinking_by, sizeof(state->thinking_by), "%s", by);
    snprintf(state->status, sizeof(state->status), "%s thinking...", by);
    if (state->request_redraw) state->request_redraw(state->redraw_ctx);
}
```

5. In `drive_turn`, replace:

```c
        if (!opponent_poll(state->thinking, &res, &key)) return 0;
        state->thinking = NULL;
```
with:
```c
        if (!opponent_poll(state->thinking, &res, &key)) return 0;
        const char *err = opponent_error(state->thinking);
        state->thinking = NULL;

        if (err) {
            snprintf(state->engine_error, sizeof(state->engine_error), "%s", err);
            state->paused = 1;
            snprintf(state->status, sizeof(state->status), "%s — paused", err);
            return 1;
        }
```
Then change `char by[32];` in `drive_turn` to `char by[48];`.

6. In the `go` branch of `handle_command`, change `char by[32];` to `char by[48];`.

7. Change the `players_apply_command` call to pass `&state->engines` instead of `NULL`. Inside `if (pc > 0) {`, before the `for` loop, add `state->engine_error[0] = '\0';`.

8. In `tui_new_game`, after `state->paused = 0;`, add `state->engine_error[0] = '\0';`.

9. Before the `if (strncmp(cmd, "depth ", 6) == 0) {` block, add:

```c
    if (strcmp(cmd, "engines") == 0) {
        if (!state->engines.count) {
            snprintf(state->status, sizeof(state->status),
                     "No engines registered — add one with e in the start menu");
            return 1;
        }
        char names[200] = "";
        for (int i = 0; i < state->engines.count; i++) {
            if (i) strncat(names, ", ", sizeof(names) - strlen(names) - 1);
            strncat(names, state->engines.e[i].name, sizeof(names) - strlen(names) - 1);
        }
        snprintf(state->status, sizeof(state->status), "Engines: %s", names);
        return 1;
    }
```

10. In the `help` status string, change `"loadfen stats quit | white|black human|engine [level]"` to `"loadfen stats engines quit | white|black human|engine [level|name]"`.

- [ ] **Step 3: Show a failed engine in the panel**

In `src/tui/panels.c`, `draw_engine_panel`, after the `if (state->thinking) { … return; }` block, add:

```c
    if (state->engine_error[0]) {
        wattron(p, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        mvw_clip(p, 1, 2, "failed");
        wattroff(p, COLOR_PAIR(CP_STATUS_ERR) | A_BOLD);
        return;
    }
```

- [ ] **Step 4: Build and run the suite**

Run: `make -B dchess 2>&1 | tee build/task7.log | tail -2; grep -c warning build/task7.log; make -B test 2>&1 | tail -1`
Expected: `0` warnings and `All suites passed.`

- [ ] **Step 5: Prepare a registry for the tmux runs**

```bash
T=$(mktemp -d); mkdir -p $T/dchess
printf '[Fake]\npath = %s/build/fake_uci\nlimit = time 100\n' "$PWD" > $T/dchess/engines.conf
echo $T > build/uci-xdg
```

- [ ] **Step 6: Verify in tmux**

Each scenario starts with `T=$(cat build/uci-xdg); tmux kill-session -t op 2>/dev/null` and ends with `tmux kill-session -t op`. Status line: `tmux capture-pane -p -t op | tail -2 | head -1`. Moves: `tmux capture-pane -p -t op | grep -oE "│ +[0-9]+\. [^│]*"`.

**A. You against the fake engine.**
```bash
tmux new-session -d -s op -x 120 -y 40 "XDG_CONFIG_HOME=$T ./dchess --white human --black Fake"
sleep 1; tmux send-keys -t op i 'e2e4' Enter; sleep 1
tmux capture-pane -p -t op | head -1; tmux capture-pane -p -t op | grep -o "engine · [A-Za-z]*"
tmux capture-pane -p -t op | tail -2 | head -1
```
Expected: the title reads `You vs Fake`, the panel title `engine · Fake`, and the status `Fake: <move> (eval -0.15, depth 2)`, with a Black reply in the moves list.

**B. The fake engine against dchess, auto-playing, with pause and undo.**
```bash
tmux new-session -d -s op -x 120 -y 40 "XDG_CONFIG_HOME=$T ./dchess --white Fake --black easy"
sleep 4; tmux capture-pane -p -t op | grep -cE "│ +[0-9]+\. "
tmux send-keys -t op ' '; sleep 0.5; tmux send-keys -t op i 'undo' Enter; sleep 0.5
tmux capture-pane -p -t op | tail -2 | head -1
```
Expected: several move rows, then `Took back 1 move — paused`.

**C. A crash mid-game, then switching to another player.**
```bash
tmux new-session -d -s op -x 120 -y 40 "FAKE_UCI_MODE=crash XDG_CONFIG_HOME=$T ./dchess --white human --black Fake"
sleep 1; tmux send-keys -t op i 'e2e4' Enter; sleep 1
tmux capture-pane -p -t op | tail -2 | head -1; tmux capture-pane -p -t op | grep -c failed
tmux send-keys -t op i 'black engine easy' Enter; sleep 0.3; tmux send-keys -t op ' '; sleep 2
tmux capture-pane -p -t op | tail -2 | head -1
```
Expected: `Fake: engine exited — paused` and the panel shows `failed`. After switching and resuming, `dchess Easy: …` replies.

**D. Quitting mid-search and mid-handshake leaves no process (Review Focus 3).**
```bash
tmux new-session -d -s op -x 120 -y 40 "FAKE_UCI_MODE=slow XDG_CONFIG_HOME=$T ./dchess --white Fake --black Fake; echo EXITED; sleep 5"
sleep 1.5; tmux send-keys -t op i 'quit' Enter; sleep 1.5
tmux capture-pane -p -t op | grep -c EXITED; pgrep -f build/fake_uci || echo NO-ENGINES-LEFT
tmux kill-session -t op
tmux new-session -d -s op -x 120 -y 40 "FAKE_UCI_MODE=mute XDG_CONFIG_HOME=$T ./dchess --white Fake --black Fake; echo EXITED; sleep 5"
sleep 1; tmux send-keys -t op i 'quit' Enter; sleep 1.5
tmux capture-pane -p -t op | grep -c EXITED; pgrep -f build/fake_uci || echo NO-ENGINES-LEFT
```
Expected: `1` and `NO-ENGINES-LEFT`, both times.

**E. Undo and player changes while it thinks (Review Focus 4).**
```bash
tmux new-session -d -s op -x 120 -y 40 "FAKE_UCI_MODE=slow XDG_CONFIG_HOME=$T ./dchess --white human --black Fake"
sleep 1; tmux send-keys -t op i 'e2e4' Enter; sleep 1
tmux send-keys -t op i 'undo' Enter; sleep 2
tmux capture-pane -p -t op | grep -cE "│ +[0-9]+\. "
tmux send-keys -t op i 'e2e4' Enter; sleep 1; tmux send-keys -t op i 'black human' Enter; sleep 2
tmux capture-pane -p -t op | grep -oE "│ +[0-9]+\. [^│]*"
tmux send-keys -t op i 'stop' Enter; sleep 0.5; tmux capture-pane -p -t op | tail -2 | head -1
```
Expected:
- After `undo`, `0` move rows: no stale move landed.
- After `black human`, only `1. e4`.
- `stop` then says `No search in progress`.

**F. The `engines` command and name commands.**
```bash
tmux new-session -d -s op -x 120 -y 40 "XDG_CONFIG_HOME=$T ./dchess --no-menu"
sleep 1; tmux send-keys -t op i 'engines' Enter; sleep 0.3; tmux capture-pane -p -t op | tail -2 | head -1
tmux send-keys -t op i 'black engine Nope' Enter; sleep 0.3; tmux capture-pane -p -t op | tail -2 | head -1
tmux send-keys -t op i 'black engine Fake' Enter; sleep 0.3; tmux capture-pane -p -t op | head -1
```
Expected: `Engines: Fake`, then `Unknown engine 'Nope'. …`, then the title `You vs Fake`.

- [ ] **Step 7: Commit**

```bash
git add headers/tui/tui.h src/tui/tui.c src/tui/commands.c src/tui/panels.c
git commit -m "feat(tui): play against UCI engines and report their failures"
```

---

### Task 8: Engines screen and the start-menu cycle

**Files:**
- Create: `headers/tui/engines_tui.h`
- Create: `src/tui/engines_tui.c`
- Modify: `src/tui/onboard.c`
- Modify: `README.md` (the onboarding paragraph)

**Interfaces:**
- Consumes: the registry API (Task 1); `uci_probe`, `UciProbe` (Task 4); `player_uci` (Task 5); `state->engines` (Task 7); `panel_frame`, `panel_shadow`, `panel_shadow_destroy`, `CP_*` pairs.
- Produces: `void engines_screen(EngineList *list);`. It is modal, returns on Esc, and saves `engines.conf` after every change.

- [ ] **Step 1: Write the Engines screen**

Create `headers/tui/engines_tui.h`:

```c
#ifndef ENGINES_TUI_H
#define ENGINES_TUI_H

#include "utils/engines.h"

/* Add, edit, test and delete engines. Saves after every change and
 * returns on Esc. */
void engines_screen(EngineList *list);

#endif
```

Create `src/tui/engines_tui.c`:

```c
#include "tui/engines_tui.h"
#include "tui/colors.h"
#include "tui/panels.h"
#include "tui/panel.h"
#include "game/uci.h"
#include <ncurses.h>
#include <stdio.h>
#include <string.h>

static const int TIME_STEPS[] = { 100, 200, 500, 1000, 2000, 3000, 5000, 10000, 30000, 60000 };
#define N_TIME_STEPS ((int)(sizeof(TIME_STEPS) / sizeof(TIME_STEPS[0])))

typedef struct {
    WINDOW *win, *shadow;
    int     h, w;
    int     cursor;       /* list->count is the "+ Add engine…" row */
    char    msg[160];
    int     msg_err;
} EngScreen;

static void build(EngScreen *s)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    s->w = cols - 2 < 72 ? cols - 2 : 72;
    s->h = rows - 2 < 20 ? rows - 2 : 20;
    if (s->w < 1) s->w = 1;
    if (s->h < 1) s->h = 1;
    int r = (rows - s->h) / 2, c = (cols - s->w) / 2;

    werase(stdscr);
    wrefresh(stdscr);
    s->shadow = panel_shadow(s->h, s->w, r, c);
    s->win = newwin(s->h, s->w, r, c);
    wbkgd(s->win, COLOR_PAIR(CP_CANVAS));
    keypad(s->win, TRUE);
}

static void destroy(EngScreen *s)
{
    delwin(s->win);
    panel_shadow_destroy(s->shadow);
}

static void say(EngScreen *s, int err, const char *text)
{
    snprintf(s->msg, sizeof(s->msg), "%s", text);
    s->msg_err = err;
}

/* Paths are ASCII in practice; a long one ends in "…". */
static void clip(const char *src, char *dst, size_t size, int width)
{
    if ((int)strlen(src) <= width) snprintf(dst, size, "%s", src);
    else snprintf(dst, size, "%.*s…", width > 1 ? width - 1 : 0, src);
}

static void blank(EngScreen *s, int row)
{
    for (int c = 1; c < s->w - 1; c++) mvwaddch(s->win, row, c, ' ');
}

static void draw(EngScreen *s, const EngineList *l)
{
    WINDOW *w = s->win;
    int inner = s->w - 4, name_w = 20, str_w = 16;
    int path_w = inner - name_w - str_w - 4;
    if (path_w < 4) path_w = 4;

    werase(w);
    panel_frame(w, "dchess · engines", CP_ACC_BOARD);
    wattron(w, COLOR_PAIR(CP_HINT));
    mvwprintw(w, 1, 4, "%-*s %-*s %s", name_w, "NAME", str_w, "STRENGTH", "PATH");
    wattroff(w, COLOR_PAIR(CP_HINT));

    int first = 2, last = s->h - 5;
    int visible = last - first + 1;
    int top = s->cursor >= visible ? s->cursor - visible + 1 : 0;
    for (int i = top; i <= l->count && first + (i - top) <= last; i++) {
        int row = first + (i - top), on = (i == s->cursor);
        if (on) wattron(w, A_REVERSE);
        if (i == l->count) {
            mvwprintw(w, row, 2, "%-*s", inner, on ? "▸ + Add engine…" : "  + Add engine…");
        } else {
            char str[48], path[256];
            engine_strength_label(&l->e[i], str, sizeof(str));
            clip(l->e[i].path, path, sizeof(path), path_w);
            mvwprintw(w, row, 2, "%s %-*.*s %-*.*s %-*s", on ? "▸" : " ",
                      name_w, name_w, l->e[i].name, str_w, str_w, str, path_w, path);
        }
        if (on) wattroff(w, A_REVERSE);
    }

    int pair = s->msg_err ? CP_STATUS_ERR : CP_STATUS_OK;
    wattron(w, COLOR_PAIR(pair));
    mvwprintw(w, s->h - 4, 2, "%-.*s", inner, s->msg);
    wattroff(w, COLOR_PAIR(pair));
    wattron(w, COLOR_PAIR(CP_HINT));
    mvwprintw(w, s->h - 3, 2, "%-.*s", inner, "a add   enter edit   t test   d delete");
    mvwprintw(w, s->h - 2, 2, "%-.*s", inner, "esc back");
    wattroff(w, COLOR_PAIR(CP_HINT));
    wrefresh(w);
}

/* Edits `buf` in place. 1 on Enter, 0 on Esc. */
static int prompt(EngScreen *s, int row, const char *label, char *buf, size_t size)
{
    WINDOW *w = s->win;
    int len = (int)strlen(buf), lab = (int)strlen(label);
    int width = s->w - 4 - lab;
    curs_set(1);
    for (;;) {
        blank(s, row);
        wattron(w, A_BOLD);
        mvwprintw(w, row, 2, "%s", label);
        wattroff(w, A_BOLD);
        int shown = len > width - 1 ? len - (width - 1) : 0;   /* keep the end visible */
        mvwprintw(w, row, 2 + lab, "%s", buf + shown);
        wmove(w, row, 2 + lab + len - shown);
        wrefresh(w);

        int ch = wgetch(w);
        if (ch == 27) { curs_set(0); return 0; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) { curs_set(0); return 1; }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len) buf[--len] = '\0';
        } else if (ch >= 32 && ch < 127 && len < (int)size - 1) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }
}

static int nearest_time(int ms)
{
    int best = 0;
    for (int i = 1; i < N_TIME_STEPS; i++) {
        int d = TIME_STEPS[i] - ms, bd = TIME_STEPS[best] - ms;
        if ((d < 0 ? -d : d) < (bd < 0 ? -bd : bd)) best = i;
    }
    return best;
}

/* ←/→ switches time and depth, ↑/↓ changes the value. */
static int pick_limit(EngScreen *s, int row, EngineEntry *e)
{
    int depth_mode = e->limit_depth > 0;
    int ti = nearest_time(e->limit_ms ? e->limit_ms : 1000);
    int depth = e->limit_depth ? e->limit_depth : 12;
    say(s, 0, "←→ time or depth   ↑↓ value   enter ok   esc cancel");
    for (;;) {
        EngineEntry tmp = *e;
        tmp.elo = 0;
        tmp.limit_depth = depth_mode ? depth : 0;
        tmp.limit_ms = TIME_STEPS[ti];
        char label[48];
        engine_strength_label(&tmp, label, sizeof(label));
        blank(s, row);
        mvwprintw(s->win, row, 2, "Limit:  ◂ %s ▸", label);
        wattron(s->win, COLOR_PAIR(CP_STATUS_OK));
        blank(s, s->h - 4);
        mvwprintw(s->win, s->h - 4, 2, "%-.*s", s->w - 4, s->msg);
        wattroff(s->win, COLOR_PAIR(CP_STATUS_OK));
        wrefresh(s->win);

        switch (wgetch(s->win)) {
        case KEY_LEFT: case KEY_RIGHT: case 'h': case 'l':
            depth_mode = !depth_mode;
            break;
        case KEY_UP: case 'k':
            if (depth_mode) { if (depth < 30) depth++; }
            else if (ti < N_TIME_STEPS - 1) ti++;
            break;
        case KEY_DOWN: case 'j':
            if (depth_mode) { if (depth > 1) depth--; }
            else if (ti > 0) ti--;
            break;
        case '\n': case '\r': case KEY_ENTER:
            e->limit_depth = depth_mode ? depth : 0;
            e->limit_ms = TIME_STEPS[ti];
            return 1;
        case 27:
            return 0;
        }
    }
}

/* off, the minimum, each hundred above it, the maximum. */
static int elo_steps(const UciProbe *p, int *out, int max)
{
    int n = 0;
    out[n++] = 0;
    if (p->elo_max <= p->elo_min) {
        out[n++] = p->elo_min;
        return n;
    }
    for (int v = p->elo_min; v <= p->elo_max && n < max; v = (v / 100 + 1) * 100)
        out[n++] = v;
    if (n < max && out[n - 1] != p->elo_max) out[n++] = p->elo_max;
    return n;
}

static int pick_elo(EngScreen *s, int row, EngineEntry *e, const UciProbe *p)
{
    int steps[64], n = elo_steps(p, steps, 64), i = 0;
    for (int k = 1; k < n; k++)
        if (e->elo && steps[k] <= e->elo) i = k;
    for (;;) {
        blank(s, row);
        if (steps[i]) mvwprintw(s->win, row, 2, "Elo:    ◂ %d ▸", steps[i]);
        else          mvwprintw(s->win, row, 2, "Elo:    ◂ off ▸");
        wrefresh(s->win);
        switch (wgetch(s->win)) {
        case KEY_LEFT: case 'h': if (i > 0) i--; break;
        case KEY_RIGHT: case 'l': if (i < n - 1) i++; break;
        case '\n': case '\r': case KEY_ENTER: e->elo = steps[i]; return 1;
        case 27: return 0;
        }
    }
}

/* Path → probe → name → limit → Elo. 1 when `e` is ready to save. */
static int form(EngScreen *s, const EngineList *l, int index, EngineEntry *e)
{
    int r_path = s->h - 8, r_name = s->h - 7, r_limit = s->h - 6, r_elo = s->h - 5;
    UciProbe probe;
    char err[160];

    for (;;) {
        draw(s, l);
        for (int r = r_path; r <= r_elo; r++) blank(s, r);
        if (!prompt(s, r_path, "Path:   ", e->path, sizeof(e->path))) return 0;
        if (!e->path[0]) continue;
        say(s, 0, "Testing…");
        draw(s, l);
        for (int r = r_path + 1; r <= r_elo; r++) blank(s, r);
        mvwprintw(s->win, r_path, 2, "Path:   %s", e->path);
        wrefresh(s->win);
        if (uci_probe(e->path, &probe, err, sizeof(err))) break;
        say(s, 1, err);
    }

    say(s, 0, "");
    if (!e->name[0])
        snprintf(e->name, sizeof(e->name), "%.40s", probe.name[0] ? probe.name : "Engine");
    for (;;) {
        draw(s, l);
        for (int r = r_path; r <= r_elo; r++) blank(s, r);
        mvwprintw(s->win, r_path, 2, "Path:   %s", e->path);
        if (!prompt(s, r_name, "Name:   ", e->name, sizeof(e->name))) return 0;
        if (engines_check_name(l, e->name, index, err, sizeof(err))) break;
        say(s, 1, err);
    }

    if (!pick_limit(s, r_limit, e)) return 0;
    if (probe.elo_supported) {
        if (!pick_elo(s, r_elo, e, &probe)) return 0;
    } else {
        e->elo = 0;
    }
    return 1;
}

static void save(EngScreen *s, const EngineList *l, const char *done)
{
    char path[512], m[160];
    if (engines_save(l)) {
        say(s, 0, done);
        return;
    }
    engines_path(path, sizeof(path));
    snprintf(m, sizeof(m), "Could not write %.140s", path);
    say(s, 1, m);
}

static void add(EngScreen *s, EngineList *l)
{
    EngineEntry e;
    char err[160], done[96];
    if (l->count >= ENGINES_MAX) {
        say(s, 1, "The list is full (32 engines)");
        return;
    }
    memset(&e, 0, sizeof(e));
    e.limit_ms = 1000;
    if (!form(s, l, -1, &e)) { say(s, 0, "Cancelled"); return; }
    if (!engines_add(l, &e, err, sizeof(err))) { say(s, 1, err); return; }
    s->cursor = l->count - 1;
    snprintf(done, sizeof(done), "Added %s", e.name);
    save(s, l, done);
}

static void edit(EngScreen *s, EngineList *l, int i)
{
    EngineEntry e = l->e[i];
    char err[160], done[96];
    if (!form(s, l, i, &e)) { say(s, 0, "Cancelled"); return; }
    if (!engines_replace(l, i, &e, err, sizeof(err))) { say(s, 1, err); return; }
    snprintf(done, sizeof(done), "Saved %s", e.name);
    save(s, l, done);
}

static void test_entry(EngScreen *s, const EngineList *l, const EngineEntry *e)
{
    UciProbe p;
    char err[160], m[160];
    say(s, 0, "Testing…");
    draw(s, l);
    if (!uci_probe(e->path, &p, err, sizeof(err))) {
        snprintf(m, sizeof(m), "✗ %s", err);
        say(s, 1, m);
        return;
    }
    int len = snprintf(m, sizeof(m), "✓ %s", p.name[0] ? p.name : e->name);
    if (p.author[0] && len < (int)sizeof(m))
        len += snprintf(m + len, sizeof(m) - len, " · by %s", p.author);
    if (p.elo_supported && len < (int)sizeof(m))
        snprintf(m + len, sizeof(m) - len, " · Elo %d–%d", p.elo_min, p.elo_max);
    say(s, 0, m);
}

static void remove_entry(EngScreen *s, EngineList *l)
{
    char m[96], name[ENGINE_NAME_MAX + 1];
    snprintf(name, sizeof(name), "%s", l->e[s->cursor].name);
    snprintf(m, sizeof(m), "Delete %s? y to confirm", name);
    say(s, 1, m);
    draw(s, l);
    if (wgetch(s->win) != 'y') { say(s, 0, "Kept"); return; }
    engines_remove(l, name);
    if (s->cursor > l->count) s->cursor = l->count;
    snprintf(m, sizeof(m), "Deleted %s", name);
    save(s, l, m);
}

void engines_screen(EngineList *list)
{
    EngScreen s;
    memset(&s, 0, sizeof(s));
    build(&s);
    if (!list->count) say(&s, 0, "No engines yet — press a to add one");

    for (;;) {
        draw(&s, list);
        int ch = wgetch(s.win);
        switch (ch) {
        case KEY_RESIZE: destroy(&s); build(&s); break;
        case KEY_UP: case 'k': if (s.cursor > 0) s.cursor--; break;
        case KEY_DOWN: case 'j': if (s.cursor < list->count) s.cursor++; break;
        case 'a': add(&s, list); break;
        case '\n': case '\r': case KEY_ENTER:
            if (s.cursor == list->count) add(&s, list);
            else edit(&s, list, s.cursor);
            break;
        case 't': if (s.cursor < list->count) test_entry(&s, list, &list->e[s.cursor]); break;
        case 'd': if (s.cursor < list->count) remove_entry(&s, list); break;
        case 27: case 'q': destroy(&s); return;
        }
    }
}
```

Run: `make -B dchess 2>&1 | grep -E "warning|error" | head; make -B dchess 2>&1 | grep -c warning`
Expected: no output from the first command, then `0`.

- [ ] **Step 2: Put the registry into the start-menu cycle**

In `src/tui/onboard.c`:

1. Add `#include "tui/engines_tui.h"` after `#include "tui/stats_tui.h"`.

2. Replace everything from `/* Each player row cycles You, then the engine at each level. */` through the end of `player_who()` with:

```c
typedef struct {
    Player sel[2];
    int    use_custom_fen;
    char   fen[128];
    int    theme;
} OnboardChoice;

/* You, dchess Easy/Medium/Hard, then every registry entry in file order. */
static int cycle_count(const EngineList *l) { return 4 + l->count; }

static Player cycle_player(const EngineList *l, int i)
{
    if (i == 0) return player_human();
    if (i <= 3) return player_builtin(i - 1);
    return player_uci(l->e[i - 4].name);
}

/* An entry that no longer exists maps to You. */
static int cycle_index(const EngineList *l, const Player *p)
{
    if (p->kind == PLAYER_BUILTIN) return p->level + 1;
    if (p->kind == PLAYER_UCI)
        for (int i = 0; i < l->count; i++)
            if (strcmp(l->e[i].name, p->engine) == 0) return i + 4;
    return 0;
}

static void choice_label(const EngineList *l, const Player *p, char *buf, size_t n)
{
    player_label(p, buf, n);
    const EngineEntry *e = p->kind == PLAYER_UCI ? engines_find(l, p->engine) : NULL;
    if (e) {
        char s[48];
        size_t len = strlen(buf);
        engine_strength_label(e, s, sizeof(s));
        snprintf(buf + len, n - len, " · %s", s);
    }
}
```

3. Replace:

```c
    choice.who[WHITE] = player_who(&state->players[WHITE]);
    choice.who[BLACK] = player_who(&state->players[BLACK]);
```
with:
```c
    for (int s = WHITE; s <= BLACK; s++)
        choice.sel[s] = cycle_player(&state->engines,
                                     cycle_index(&state->engines, &state->players[s]));
```

4. In the drawing loop, replace:

```c
            Player p = who_player(choice.who[side]);
            char label[32];
            player_label(&p, label, sizeof(label));
```
with:
```c
            char label[96];
            choice_label(&state->engines, &choice.sel[side], label, sizeof(label));
```

5. Replace the two hint lines with:

```c
        mvwprintw(win, ph - 3, 2, "%-.*s", hint_w, "↑↓ move   ←→ change   e engines   s stats");
        mvwprintw(win, ph - 2, 2, "%-.*s", hint_w, "enter select   esc quit");
```

6. In `KEY_LEFT`, replace `choice.who[s] = (choice.who[s] + WHO_COUNT - 1) % WHO_COUNT;` with:

```c
                    int c = cycle_count(&state->engines);
                    int i = cycle_index(&state->engines, &choice.sel[s]);
                    choice.sel[s] = cycle_player(&state->engines, (i + c - 1) % c);
```
In `KEY_RIGHT`, replace `choice.who[s] = (choice.who[s] + 1) % WHO_COUNT;` with:

```c
                    int c = cycle_count(&state->engines);
                    int i = cycle_index(&state->engines, &choice.sel[s]);
                    choice.sel[s] = cycle_player(&state->engines, (i + 1) % c);
```

7. Before `case 's': case 'S': {`, add:

```c
            case 'e': case 'E':
                engines_screen(&state->engines);
                engines_load(&state->engines);
                for (int s = WHITE; s <= BLACK; s++)
                    choice.sel[s] = cycle_player(&state->engines,
                                                 cycle_index(&state->engines, &choice.sel[s]));
                werase(stdscr);
                refresh();
                break;
```

8. Replace:

```c
    chosen.players[WHITE] = who_player(choice.who[WHITE]);
    chosen.players[BLACK] = who_player(choice.who[BLACK]);
```
with:
```c
    chosen.players[WHITE] = choice.sel[WHITE];
    chosen.players[BLACK] = choice.sel[BLACK];
```

- [ ] **Step 3: Update the README**

In `README.md`, after the sentence `Pressing \`s\` from that screen shows your stats;`, add:

```
Pressing `e` opens the Engines screen, where you add a UCI engine by its path
(dchess starts it and reads its name), set its strength (time per move or
depth, plus an Elo cap when the engine supports one), test it, or delete it.
Registered engines then appear in the White and Black choices.
```

- [ ] **Step 4: Build and run the suite**

Run: `make -B dchess 2>&1 | tee build/task8.log | tail -2; grep -c warning build/task8.log; grep -n "who_player\|player_who\|WHO_COUNT\|choice.who" src/tui/onboard.c; make -B test 2>&1 | tail -1`
Expected: `0` warnings, no `grep` matches, and `All suites passed.`

- [ ] **Step 5: Verify in tmux**

Each scenario starts with `T=$(mktemp -d); tmux kill-session -t op 2>/dev/null` (a fresh, empty registry) and ends with `tmux kill-session -t op`.

**A. Adding an engine, then playing it.**
```bash
tmux new-session -d -s op -x 120 -y 40 "XDG_CONFIG_HOME=$T ./dchess"
sleep 1; tmux send-keys -t op e; sleep 0.5; tmux capture-pane -p -t op | grep -E "No engines|Add engine"
tmux send-keys -t op a; sleep 0.3; tmux send-keys -t op "$PWD/build/fake_uci" Enter; sleep 1
tmux send-keys -t op Enter; sleep 0.3; tmux send-keys -t op Enter; sleep 0.5
tmux capture-pane -p -t op | grep -E "Fake UCI|Added"; cat $T/dchess/engines.conf
tmux send-keys -t op Escape; sleep 1.5; tmux send-keys -t op Down Right Right; sleep 0.3
tmux capture-pane -p -t op | grep -E "Black:"
```
Expected:
- The empty list shows `No engines yet` and `+ Add engine…`.
- After the path, the name defaults to `Fake UCI`, and the list shows `Fake UCI   1s/move   /…/build/fake_uci` with `Added Fake UCI`.
- `engines.conf` has the section.
- Back in the menu, Right twice on Black from `dchess Medium` passes `dchess Hard` and reaches `Black: Fake UCI · 1s/move`.

**B. An Elo-capable engine, and test.**
```bash
tmux new-session -d -s op -x 120 -y 40 "FAKE_UCI_MODE=elo XDG_CONFIG_HOME=$T ./dchess"
sleep 1; tmux send-keys -t op e; sleep 0.3; tmux send-keys -t op a; sleep 0.3
tmux send-keys -t op "$PWD/build/fake_uci" Enter; sleep 1
tmux send-keys -t op BSpace BSpace BSpace BSpace BSpace BSpace BSpace BSpace "Capped" Enter; sleep 0.3
tmux send-keys -t op Up Enter; sleep 0.3; tmux send-keys -t op Right Right Enter; sleep 0.5
cat $T/dchess/engines.conf
tmux send-keys -t op t; sleep 1; tmux capture-pane -p -t op | grep -E "✓"
```
Expected:
- The file has `[Capped]`, `limit = time 2000` and `elo   = 1400`. Right twice from `off` goes 1320, then 1400.
- The test line reads `✓ Fake UCI · by dchess tests · Elo 1320–3190`.

**C. A bad path.**
```bash
tmux new-session -d -s op -x 120 -y 40 "XDG_CONFIG_HOME=$T ./dchess"
sleep 1; tmux send-keys -t op e; sleep 0.3; tmux send-keys -t op a; sleep 0.3
tmux send-keys -t op "/nonexistent/engine" Enter; sleep 1
tmux capture-pane -p -t op | grep -E "could not start"; tmux send-keys -t op Escape; sleep 1.5
tmux capture-pane -p -t op | grep -E "Cancelled"
```
Expected: `could not start /nonexistent/engine` in red, and the path prompt still open. Esc gives `Cancelled`.

**D. Deleting the engine a side is set to.**
```bash
mkdir -p $T/dchess; printf '[Fake]\npath = %s/build/fake_uci\n' "$PWD" > $T/dchess/engines.conf
tmux new-session -d -s op -x 120 -y 40 "XDG_CONFIG_HOME=$T ./dchess"
sleep 1; tmux send-keys -t op Down Right Right; sleep 0.3; tmux capture-pane -p -t op | grep "Black:"
tmux send-keys -t op e; sleep 0.3; tmux send-keys -t op d y Escape; sleep 1.5
tmux capture-pane -p -t op | grep "Black:"
```
Expected: `Black: Fake · 1s/move`, then `Black: You` after the delete.

- [ ] **Step 6: Commit**

```bash
git add headers/tui/engines_tui.h src/tui/engines_tui.c src/tui/onboard.c README.md
git commit -m "feat(tui): Engines screen and registered engines in the start menu"
```
