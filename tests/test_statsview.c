/* The stats page's numbers.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "game/statsview.h"

static int failures = 0;
static char dir[] = "/tmp/dchess-statsview-XXXXXX";
static char path[512];
static const long NOW = 1800000000L;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* One game, `days` before NOW. Kinds: profile, guest, dchess, engine. */
static void game(FILE *f, double days, const char *w, const char *wk, const char *b,
                 const char *bk, const char *strength, const char *result,
                 const char *end, int plies)
{
    time_t t = (time_t)(NOW - (long)(days * 86400));
    struct tm tm;
    char d[16], h[16];
    localtime_r(&t, &tm);
    strftime(d, sizeof(d), "%Y.%m.%d", &tm);
    strftime(h, sizeof(h), "%H:%M:%S", &tm);
    fprintf(f, "[Event \"x\"]\n[Date \"%s\"]\n[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n"
               "[Time \"%s\"]\n[WhiteKind \"%s\"]\n[BlackKind \"%s\"]\n",
            d, w, b, result, h, wk, bk);
    if (strength[0]) fprintf(f, "[BlackStrength \"%s\"]\n", strength);
    fprintf(f, "[EndReason \"%s\"]\n[PlyCount \"%d\"]\n[Seconds \"%d\"]\n\n%s\n\n",
            end, plies, plies * 6, result);
}

static const SvOpponent *row(const StatsView *v, const char *name)
{
    for (int i = 0; i < v->opp_count; i++)
        if (strcmp(v->opp[i].name, name) == 0) return &v->opp[i];
    return NULL;
}

static void test_full(void)
{
    printf("== a mixed history ==\n");
    snprintf(path, sizeof(path), "%s/games.pgn", dir);
    FILE *f = fopen(path, "w");
    game(f, 40, "saeed", "profile", "dchess", "dchess", "", "1-0", "legacy", 0);
    game(f, 20, "saeed", "profile", "Guest", "guest", "", "0-1", "checkmate", 30);
    game(f, 10, "saeed", "profile", "alice", "profile", "", "1/2-1/2", "repetition", 40);
    game(f, 3, "alice", "profile", "saeed", "profile", "", "0-1", "checkmate", 20);
    game(f, 1, "saeed", "profile", "dchess (Hard)", "dchess", "Hard", "1-0", "checkmate", 50);
    game(f, 1, "saeed", "profile", "dchess (Hard)", "dchess", "Hard", "0-1", "checkmate", 60);
    game(f, 0, "Fake", "engine", "saeed", "profile", "", "0-1", "resigned", 10);
    fclose(f);

    Profile p;
    memset(&p, 0, sizeof(p));
    snprintf(p.name, sizeof(p.name), "saeed");
    p.legacy_games = 4; p.legacy_wins = 2; p.legacy_losses = 1; p.legacy_draws = 1;

    RecordList l;
    StatsView v;
    records_load(path, &l);
    stats_view_build(&l, &p, NOW, &v);

    check("totals include the legacy line",
          v.total.games == 10 && v.total.wins == 5 && v.total.losses == 3 && v.total.draws == 2);
    check("as White: 4 games, 1-1-2",
          v.as_white.games == 4 && v.as_white.wins == 1 && v.as_white.draws == 1 && v.as_white.losses == 2);
    check("as Black: 2 wins", v.as_black.games == 2 && v.as_black.wins == 2);
    check("the current streak is W1", v.streak == 'W' && v.streak_len == 1);
    check("the best winning streak is 2", v.best_win_streak == 2);
    check("games per week: 4, 1, 1, 0",
          v.per_week[0] == 4 && v.per_week[1] == 1 && v.per_week[2] == 1 && v.per_week[3] == 0);

    const SvOpponent *h = row(&v, "dchess Hard"), *a = row(&v, "alice");
    check("the legacy line is the top row", v.opp_count == 5 && strcmp(v.opp[0].name, "before profiles") == 0);
    check("dchess Hard is 1-0-1", h && h->games == 2 && h->wins == 1 && h->losses == 1);
    check("alice is 1-1-0", a && a->wins == 1 && a->draws == 1);
    check("Guest and Fake have rows", row(&v, "Guest") && row(&v, "Fake"));
    check("no row for the legacy stub's 'dchess'", row(&v, "dchess") == NULL);
    check("last played is kept", h && h->last == records_time(&l.r[5]));
    check("rows are sorted by games", v.opp[1].games >= v.opp[2].games && v.opp[3].games >= v.opp[4].games);

    check("mates 4, resigns 1, repetitions 1", v.mates == 4 && v.resigns == 1 && v.repetitions == 1);
    check("average 35 moves over recorded games", v.avg_plies == 35);
    check("recent is newest first, legacy included",
          v.recent_count == 7 && strcmp(v.recent[0]->white, "Fake") == 0);
    check("the trend covers every game", v.trend_count == 7);
    records_free(&l);
}

static void test_empty(void)
{
    printf("== no games ==\n");
    RecordList l = { NULL, 0, 0 };
    Profile p;
    StatsView v;
    memset(&p, 0, sizeof(p));
    snprintf(p.name, sizeof(p.name), "nobody");
    stats_view_build(&l, &p, NOW, &v);
    check("everything is zero",
          v.total.games == 0 && v.opp_count == 0 && v.trend_count == 0 && v.streak == 0 &&
          v.avg_plies == 0 && v.recent_count == 0);
}

static void test_many_opponents(void)
{
    printf("== many opponents ==\n");
    snprintf(path, sizeof(path), "%s/many.pgn", dir);
    FILE *f = fopen(path, "w");
    char name[16];
    for (int i = 0; i < 70; i++) {
        snprintf(name, sizeof(name), "E%d", i);
        game(f, 1, "saeed", "profile", name, "engine", "", "1-0", "checkmate", 10);
    }
    fclose(f);
    Profile p;
    memset(&p, 0, sizeof(p));
    snprintf(p.name, sizeof(p.name), "saeed");
    RecordList l;
    StatsView v;
    records_load(path, &l);
    stats_view_build(&l, &p, NOW, &v);
    check("opponents are capped", v.opp_count == SV_OPP_MAX);
    check("but every game counts in the totals", v.total.games == 70);
    records_free(&l);
}

int main(void)
{
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    test_full();
    test_empty();
    test_many_opponents();

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    if (system(cmd) != 0) printf("  (could not clean %s)\n", dir);

    if (failures) {
        printf("\n%d stats view test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll stats view tests passed.\n");
    return 0;
}
