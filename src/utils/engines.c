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
