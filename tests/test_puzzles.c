/* The bundled puzzle set, and the rules of solving one.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/puzzles.h"
#include "engine/move.h"
#include "utils/bitboard.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Plays a puzzle's whole line into g; the number of plies, or -1 at a bad move. */
static int play_line(GameState *g, const PuzzleData *p)
{
    memset(g, 0, sizeof(*g));
    game_reset(g);
    if (!game_load_fen(g, p->fen)) return -1;
    char buf[128], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", p->moves);
    int n = 0;
    for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save), n++) {
        int from, to, promo;
        Move m;
        if (!parse_move_str(t, &from, &to, &promo) || !game_find_move(g, from, to, promo, &m)) return -1;
        game_play(g, m);
    }
    game_update_status(g);
    return n;
}

static void test_data(void)
{
    printf("== the puzzle set ==\n");
    static GameState g;
    int legal = 1, length = 1, mates = 1, sorted = 1, unique = 1, buckets = 1;
    int in_bucket[30] = { 0 };
    for (int i = 0; i < puzzle_count; i++) {
        const PuzzleData *p = &puzzle_data[i];
        int n = play_line(&g, p);
        if (n < 0) { legal = 0; printf("    illegal: %s\n", p->id); continue; }
        if (n < 2 || n > PUZZLE_PLIES) length = 0;
        if ((p->themes & TH_ANY_MATE) && !(g.game_over && !strncmp(g.result, "Checkmate", 9))) {
            mates = 0;
            printf("    no mate: %s\n", p->id);
        }
        if (i && p->rating < puzzle_data[i - 1].rating) sorted = 0;
        if (p->rating / 100 < 30) in_bucket[p->rating / 100]++;
    }
    for (int i = 0; i < puzzle_count && unique; i++)
        if (puzzle_find(puzzle_data[i].id) != i) unique = 0;
    for (int b = 6; b <= 27; b++) if (!in_bucket[b]) { buckets = 0; printf("    empty bucket %d00\n", b); }
    check("there are enough puzzles", puzzle_count >= 2500);
    check("every FEN loads and every move is legal", legal);
    check("every line is 2 to PUZZLE_PLIES plies", length);
    check("mate puzzles end in checkmate", mates);
    check("the set is sorted by rating", sorted);
    check("the ids are unique and found", unique);
    check("every 100-point bucket from 600 to 2700 has puzzles", buckets);
    check("themes have names", !strcmp(puzzle_theme_name(TH_MATE1), "mate in 1") &&
                               !strcmp(puzzle_theme_name(TH_FORK), "fork"));
    check("an unknown id is not found", puzzle_find("nope!") == -1);
}

int main(void)
{
    init_attacks();
    test_data();
    if (failures) {
        printf("\n%d puzzle test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll puzzle tests passed.\n");
    return 0;
}
