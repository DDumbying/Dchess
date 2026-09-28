/* The puzzle rating, choosing the next puzzle, and the profile's puzzle file.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "game/puzzle_stats.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void test_rating(void)
{
    printf("== the rating ==\n");
    check("a solve at equal ratings gains 20 early on", puzzle_rating_after(1500, 0, 1500, 1) == 1520);
    check("a fail loses 20", puzzle_rating_after(1500, 0, 1500, 0) == 1480);
    check("after 20 puzzles the step halves", puzzle_rating_after(1500, 25, 1500, 1) == 1510);
    check("a hard puzzle solved gains more", puzzle_rating_after(1500, 25, 1900, 1) > 1515);
    check("the rating never drops below 400", puzzle_rating_after(405, 30, 405, 0) == 400);
}

static void test_record(void)
{
    printf("== recording results ==\n");
    static PuzzleStats s;
    puzzle_stats_init(&s);
    check("a new profile starts at 1500", s.rating == 1500 && !s.played && !s.nmissed);
    int a = 0;
    while (puzzle_data[a].rating < 1500) a++;        /* two puzzles at about the solver's rating */
    puzzle_stats_record(&s, a, 1);
    puzzle_stats_record(&s, a + 1, 1);
    check("solves count, streak and mark seen", s.played == 2 && s.streak == 2 && s.best_streak == 2 &&
                                                s.seen[a] && s.seen[a + 1] && s.rating > 1500);
    puzzle_stats_record(&s, 12, 0);
    puzzle_stats_record(&s, 12, 0);
    check("a fail ends the streak and is missed once", s.streak == 0 && s.best_streak == 2 &&
                                                         s.nmissed == 1 && s.missed[0] == 12);
    puzzle_stats_record(&s, 13, 0);
    puzzle_stats_record(&s, 12, 1);
    check("a solve takes it off the missed list", s.nmissed == 1 && s.missed[0] == 13);
    int r = s.rating, st = s.streak;
    puzzle_stats_forgive(&s, 13);
    check("forgiving leaves the rating and streak", s.nmissed == 0 && s.rating == r && s.streak == st);
}

static int near(int i, int target, int w)
{
    return abs(puzzle_data[i].rating - target) <= w;
}

static void test_pick(void)
{
    printf("== choosing the next puzzle ==\n");
    static unsigned char seen[PUZZLE_MAX];
    unsigned seed = 1;
    memset(seen, 0, sizeof(seen));
    int i = puzzle_pick(seen, 1500, 0, &seed);
    check("an unseen puzzle within 100 of the rating", i >= 0 && near(i, 1500, 100));
    for (int k = 0; k < puzzle_count; k++) if (near(k, 1500, 300)) seen[k] = 1;
    i = puzzle_pick(seen, 1500, 0, &seed);
    check("the window widens once those are seen", i >= 0 && !seen[i] && !near(i, 1500, 300));
    memset(seen, 1, sizeof(seen));
    i = puzzle_pick(seen, 1500, 0, &seed);
    check("all seen: the list starts over", i >= 0 && near(i, 1500, 100));
    memset(seen, 0, sizeof(seen));
    int forks = 1;
    for (int n = 0; n < 50; n++) {
        i = puzzle_pick(seen, 1200, TH_FORK, &seed);
        if (i < 0 || !(puzzle_data[i].themes & TH_FORK)) forks = 0;
    }
    check("a theme gives only that theme", forks);
    int others_kept = 1;
    memset(seen, 1, sizeof(seen));
    for (int k = 0; k < puzzle_count; k++) if (puzzle_data[k].themes & TH_FORK) seen[k] = 1;
    i = puzzle_pick(seen, 1200, TH_FORK, &seed);
    for (int k = 0; k < puzzle_count; k++) if (!(puzzle_data[k].themes & TH_FORK) && !seen[k]) others_kept = 0;
    check("every fork seen: only the forks start over", i >= 0 && others_kept);
}

static void test_storage(void)
{
    printf("== the puzzle file ==\n");
    char dir[] = "/tmp/dchess-pz-XXXXXX", path[256];
    if (!mkdtemp(dir)) { perror("mkdtemp"); failures++; return; }
    static PuzzleStats s, t;
    puzzle_stats_init(&s);
    s.rating = 1633; s.played = 41; s.streak = 3; s.best_streak = 9; s.rush_best = 17;
    s.seen[0] = s.seen[5] = 1;
    s.missed[0] = 7; s.missed[1] = 2; s.nmissed = 2;
    check("a profile's stats are saved", puzzle_stats_save(dir, "alice", &s));
    check("and read back the same", puzzle_stats_load(dir, "alice", &t) == 1 && t.rating == 1633 &&
          t.played == 41 && t.streak == 3 && t.best_streak == 9 && t.rush_best == 17 &&
          t.seen[0] && t.seen[5] && !t.seen[1] && t.nmissed == 2 && t.missed[0] == 7 && t.missed[1] == 2);
    check("a missing file gives the defaults", puzzle_stats_load(dir, "bob", &t) == 0 && t.rating == 1500);

    snprintf(path, sizeof(path), "%s/carol.txt", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "rating 1700\nthis is not a line\nmissed nope! %s\nseen zz%%zz %s\nstreak 4\n",
            puzzle_data[3].id, puzzle_data[4].id);
    fclose(f);
    check("a damaged file loads what it can", puzzle_stats_load(dir, "carol", &t) == 1 && t.rating == 1700 &&
          t.streak == 4 && t.nmissed == 1 && t.missed[0] == 3 && t.seen[4]);

    check("a rename moves the file", puzzle_stats_rename(dir, "alice", "alicia") &&
          puzzle_stats_load(dir, "alicia", &t) == 1 && t.rating == 1633 &&
          puzzle_stats_load(dir, "alice", &t) == 0);
    check("renaming a profile with no file is fine", puzzle_stats_rename(dir, "nobody", "someone"));
    check("remove deletes it", puzzle_stats_remove(dir, "alicia") && puzzle_stats_load(dir, "alicia", &t) == 0);
    check("names with a slash or a leading dot are refused",
          !puzzle_stats_save(dir, "../evil", &s) && !puzzle_stats_save(dir, "a/b", &s) &&
          !puzzle_stats_save(dir, "", &s) && !puzzle_stats_remove(dir, ".."));

    snprintf(path, sizeof(path), "%s/carol.txt", dir);
    unlink(path);
    rmdir(dir);
}

int main(void)
{
    test_rating();
    test_record();
    test_pick();
    test_storage();
    if (failures) {
        printf("\n%d puzzle stats test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll puzzle stats tests passed.\n");
    return 0;
}
