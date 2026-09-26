#include "game/profiles.h"
#include "game/records.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *RESERVED[] = { "easy", "medium", "hard", "human", "guest" };

int profiles_path(char *buf, size_t n)
{
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])        snprintf(buf, n, "%s/dchess/profiles.conf", xdg);
    else if (home && home[0]) snprintf(buf, n, "%s/.config/dchess/profiles.conf", home);
    else return 0;
    return 1;
}

static void set_err(char *err, size_t n, const char *msg)
{
    if (err && n) snprintf(err, n, "%s", msg);
}

int profiles_check_name(const ProfileList *l, const EngineList *e, const char *name,
                        int skip, char *err, size_t n)
{
    size_t len = strlen(name);
    if (len == 0 || len > PROFILE_NAME_MAX) {
        set_err(err, n, "a name needs 1 to 24 characters");
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
            set_err(err, n, "that name is reserved");
            return 0;
        }
    if (e && engines_find(e, name)) {
        set_err(err, n, "an engine already has that name");
        return 0;
    }
    for (int i = 0; i < l->count; i++)
        if (i != skip && strcmp(l->p[i].name, name) == 0) {
            set_err(err, n, "a profile with that name already exists");
            return 0;
        }
    return 1;
}

int profiles_add(ProfileList *l, const EngineList *e, const char *name, char *err, size_t n)
{
    if (l->count >= PROFILES_MAX) {
        set_err(err, n, "the list is full (32 profiles)");
        return 0;
    }
    if (!profiles_check_name(l, e, name, -1, err, n)) return 0;
    Profile *p = &l->p[l->count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    return 1;
}

int profiles_rename(ProfileList *l, const EngineList *e, int i, const char *name,
                    const char *games, char *err, size_t n)
{
    if (i < 0 || i >= l->count) {
        set_err(err, n, "no such profile");
        return 0;
    }
    if (!profiles_check_name(l, e, name, i, err, n)) return 0;
    if (games && !records_rename(games, l->p[i].name, name)) {
        set_err(err, n, "could not relabel the game history");
        return 0;
    }
    snprintf(l->p[i].name, sizeof(l->p[i].name), "%s", name);
    return 1;
}

int profiles_remove(ProfileList *l, int i)
{
    if (i < 0 || i >= l->count || l->count == 1) return 0;
    memmove(&l->p[i], &l->p[i + 1], (size_t)(l->count - i - 1) * sizeof(l->p[0]));
    l->count--;
    if (l->active > i || l->active >= l->count) l->active = l->active > 0 ? l->active - 1 : 0;
    return 1;
}

int profiles_find(const ProfileList *l, const char *name)
{
    for (int i = 0; i < l->count; i++)
        if (strcmp(l->p[i].name, name) == 0) return i;
    return -1;
}

int profiles_names(const ProfileList *l, const char **out, int max)
{
    int n = 0;
    if (l->count && n < max) out[n++] = l->p[l->active].name;
    for (int i = 0; i < l->count && n < max; i++)
        if (i != l->active) out[n++] = l->p[i].name;
    return n;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

#define COPY(dst, v) snprintf(dst, sizeof(dst), "%.*s", (int)sizeof(dst) - 1, v)

int profiles_load(ProfileList *l)
{
    char path[512], line[512], active[64] = "";
    memset(l, 0, sizeof(*l));
    if (!profiles_path(path, sizeof(path))) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    Profile *cur = NULL;
    while (fgets(line, sizeof(line), f)) {
        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';
        char *s = trim(line);
        if (!*s) continue;

        if (*s == '[') {
            cur = NULL;
            char *close = strchr(s, ']');
            if (!close) continue;
            *close = '\0';
            if (profiles_add(l, NULL, trim(s + 1), NULL, 0)) cur = &l->p[l->count - 1];
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s), *val = trim(eq + 1);
        if (!cur) {
            if (strcmp(key, "active") == 0) COPY(active, val);
        } else if (strcmp(key, "theme") == 0) {
            COPY(cur->theme, val);
        } else if (strcmp(key, "white") == 0) {
            COPY(cur->white, val);
        } else if (strcmp(key, "black") == 0) {
            COPY(cur->black, val);
        } else if (strcmp(key, "legacy") == 0) {
            int g, w, lo, d;
            if (sscanf(val, "%d %d %d %d", &g, &w, &lo, &d) == 4 && g >= 0) {
                cur->legacy_games = g;
                cur->legacy_wins = w;
                cur->legacy_losses = lo;
                cur->legacy_draws = d;
            }
        }
    }
    fclose(f);
    int a = profiles_find(l, active);
    l->active = a >= 0 ? a : 0;
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

int profiles_save(const ProfileList *l)
{
    char path[512], tmp[600];
    if (!profiles_path(path, sizeof(path)) || !make_dirs(path)) return 0;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;

    if (l->count) fprintf(f, "active = %s\n", l->p[l->active].name);
    for (int i = 0; i < l->count; i++) {
        const Profile *p = &l->p[i];
        fprintf(f, "\n[%s]\n", p->name);
        if (p->theme[0]) fprintf(f, "theme  = %s\n", p->theme);
        if (p->white[0]) fprintf(f, "white  = %s\n", p->white);
        if (p->black[0]) fprintf(f, "black  = %s\n", p->black);
        if (p->legacy_games)
            fprintf(f, "legacy = %d %d %d %d\n", p->legacy_games, p->legacy_wins,
                    p->legacy_losses, p->legacy_draws);
    }
    int ok = !ferror(f);
    if (fclose(f) != 0 || !ok) {
        remove(tmp);
        return 0;
    }
    return rename(tmp, path) == 0;
}

int profiles_first_run(ProfileList *l, const char *user, const DchessStats *old,
                       const char *games)
{
    char path[512];
    if (!profiles_path(path, sizeof(path))) return 0;
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        profiles_load(l);
        return 0;
    }

    memset(l, 0, sizeof(*l));
    if (!user || !profiles_add(l, NULL, user, NULL, 0))
        profiles_add(l, NULL, "player", NULL, 0);

    if (old) {
        Profile *p = &l->p[0];
        for (int i = 0; i < 3; i++) {
            p->legacy_games  += old->games_played[i];
            p->legacy_wins   += old->wins[i];
            p->legacy_losses += old->losses[i];
            p->legacy_draws  += old->draws[i];
        }
        int n = old->history_count < DCHESS_MAX_HISTORY ? old->history_count : DCHESS_MAX_HISTORY;
        for (int i = 0; games && i < n; i++)
            records_append_legacy(games, p->name, old->history[i].timestamp, old->history[i].result);
    }
    profiles_save(l);
    return 1;
}
