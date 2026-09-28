#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "game/puzzle_stats.h"

void puzzle_stats_init(PuzzleStats *s)
{
    memset(s, 0, sizeof(*s));
    s->rating = 1500;
    s->pending = -1;
}

int puzzle_stats_dir(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
    static const char *const parts[] = { "/.local", "/.local/share", "/.local/share/dchess",
                                         "/.local/share/dchess/puzzles" };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        snprintf(buf, n, "%s%s", home, parts[i]);
        mkdir(buf, 0755);
    }
    return 1;
}

int puzzle_stats_can_save(const char *profile)
{
    return profile[0] && profile[0] != '.' && !strchr(profile, '/');
}

static int file_for(const char *dir, const char *profile, char *buf, size_t n)
{
    if (!puzzle_stats_can_save(profile)) return 0;
    return snprintf(buf, n, "%s/%s.txt", dir, profile) < (int)n;
}

static void add_missed(PuzzleStats *s, int index)
{
    for (int i = 0; i < s->nmissed; i++) if (s->missed[i] == index) return;
    if (s->nmissed < PUZZLE_MAX) s->missed[s->nmissed++] = index;
}

int puzzle_stats_load(const char *dir, const char *profile, PuzzleStats *s)
{
    char path[512];
    puzzle_stats_init(s);
    if (!file_for(dir, profile, path, sizeof(path))) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
    if (!text) { fclose(f); return 0; }
    text[fread(text, 1, (size_t)size, f)] = '\0';
    fclose(f);

    char *lsave = NULL;
    for (char *line = strtok_r(text, "\n", &lsave); line; line = strtok_r(NULL, "\n", &lsave)) {
        char *save = NULL, *key = strtok_r(line, " ", &save);
        if (!key) continue;
        struct { const char *name; int *field; } nums[] = {
            { "rating", &s->rating }, { "played", &s->played }, { "streak", &s->streak },
            { "best_streak", &s->best_streak }, { "rush_best", &s->rush_best },
        };
        for (size_t i = 0; i < sizeof(nums) / sizeof(nums[0]); i++) {
            if (strcmp(key, nums[i].name)) continue;
            char *v = strtok_r(NULL, " ", &save), *end;
            long n = v ? strtol(v, &end, 10) : -1;
            if (v && !*end && n >= 0 && n < 100000) *nums[i].field = (int)n;
        }
        if (!strcmp(key, "pending")) {
            char *id = strtok_r(NULL, " ", &save);
            if (id) s->pending = puzzle_find(id);
            continue;
        }
        int is_seen = !strcmp(key, "seen"), is_missed = !strcmp(key, "missed");
        if (!is_seen && !is_missed) continue;
        for (char *id = strtok_r(NULL, " ", &save); id; id = strtok_r(NULL, " ", &save)) {
            int k = puzzle_find(id);
            if (k < 0) continue;
            if (is_seen) s->seen[k] = 1;
            else add_missed(s, k);
        }
    }
    free(text);
    if (s->rating < 400)  s->rating = 400;
    if (s->rating > 3500) s->rating = 3500;
    return 1;
}

int puzzle_stats_save(const char *dir, const char *profile, const PuzzleStats *s)
{
    char path[512], tmp[520];
    if (!file_for(dir, profile, path, sizeof(path))) return 0;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    fprintf(f, "rating %d\nplayed %d\nstreak %d\nbest_streak %d\nrush_best %d\nseen",
            s->rating, s->played, s->streak, s->best_streak, s->rush_best);
    for (int i = 0; i < puzzle_count; i++) if (s->seen[i]) fprintf(f, " %s", puzzle_data[i].id);
    fprintf(f, "\nmissed");
    for (int i = 0; i < s->nmissed; i++) fprintf(f, " %s", puzzle_data[s->missed[i]].id);
    fputc('\n', f);
    if (s->pending >= 0) fprintf(f, "pending %s\n", puzzle_data[s->pending].id);
    int ok = !ferror(f);
    if (fclose(f) || !ok) { remove(tmp); return 0; }
    return rename(tmp, path) == 0;
}

int puzzle_stats_rename(const char *dir, const char *from, const char *to)
{
    char a[512], b[512];
    if (!file_for(dir, from, a, sizeof(a)) || !file_for(dir, to, b, sizeof(b))) return 0;
    return rename(a, b) == 0 || errno == ENOENT;
}

int puzzle_stats_remove(const char *dir, const char *profile)
{
    char path[512];
    if (!file_for(dir, profile, path, sizeof(path))) return 0;
    return remove(path) == 0 || errno == ENOENT;
}

int puzzle_rating_after(int rating, int played, int puzzle_rating, int solved)
{
    double expected = 1.0 / (1.0 + pow(10.0, (puzzle_rating - rating) / 400.0));
    int k = played < 20 ? 40 : 20;
    int next = rating + (int)lround(k * ((solved ? 1.0 : 0.0) - expected));
    return next < 400 ? 400 : next;
}

void puzzle_stats_forgive(PuzzleStats *s, int index)
{
    for (int i = 0; i < s->nmissed; i++)
        if (s->missed[i] == index) {
            memmove(&s->missed[i], &s->missed[i + 1], (size_t)(s->nmissed - i - 1) * sizeof(int));
            s->nmissed--;
            return;
        }
}

void puzzle_stats_record(PuzzleStats *s, int index, int solved)
{
    s->rating = puzzle_rating_after(s->rating, s->played, puzzle_data[index].rating, solved);
    s->played++;
    s->seen[index] = 1;
    if (s->pending == index) s->pending = -1;
    if (solved) {
        if (++s->streak > s->best_streak) s->best_streak = s->streak;
        puzzle_stats_forgive(s, index);
    } else {
        s->streak = 0;
        add_missed(s, index);
    }
}

static int matches(int i, unsigned themes)
{
    return !themes || (puzzle_data[i].themes & themes);
}

int puzzle_pick(unsigned char seen[PUZZLE_MAX], int target, unsigned themes, unsigned *seed)
{
    for (int round = 0; round < 2; round++) {
        for (int w = 100; w <= 3000; w += 100) {
            int count = 0;
            for (int i = 0; i < puzzle_count; i++)
                count += !seen[i] && matches(i, themes) && abs(puzzle_data[i].rating - target) <= w;
            if (!count) continue;
            int pick = rand_r(seed) % count;
            for (int i = 0; i < puzzle_count; i++)
                if (!seen[i] && matches(i, themes) && abs(puzzle_data[i].rating - target) <= w && !pick--) {
                    seen[i] = 1;
                    return i;
                }
        }
        for (int i = 0; i < puzzle_count; i++) if (matches(i, themes)) seen[i] = 0;
    }
    return -1;
}

int puzzle_stats_next_rated(PuzzleStats *s, unsigned *seed)
{
    if (s->pending < 0) s->pending = puzzle_pick(s->seen, s->rating, 0, seed);
    return s->pending;
}
