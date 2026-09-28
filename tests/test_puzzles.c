/* The bundled puzzle set, and the rules of solving one.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/puzzles.h"
#include "engine/move.h"
#include "engine/movegen.h"
#include "engine/make.h"
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

/* The first puzzle with all of `themes` whose line has `plies` plies (0 = any). */
static int find_puzzle(int themes, int plies)
{
    for (int i = 0; i < puzzle_count; i++) {
        if ((puzzle_data[i].themes & themes) != themes) continue;
        int n = 1;
        for (const char *c = puzzle_data[i].moves; *c; c++) n += *c == ' ';
        if (!plies || n == plies) return i;
    }
    return -1;
}

static int mates(const GameState *g, Move m)
{
    static GameState t;
    t = *g;
    game_play(&t, m);
    game_update_status(&t);
    return t.game_over && !strncmp(t.result, "Checkmate", 9);
}

/* A legal move that is not `not`, and does not mate. */
static Move other_move(const GameState *g, Move not)
{
    MoveList ml;
    generate_moves(&g->pos, &ml);
    for (int i = 0; i < ml.count; i++) {
        Position p = g->pos;
        if (ml.moves[i] == not || !make_move(&p, ml.moves[i]) || mates(g, ml.moves[i])) continue;
        return ml.moves[i];
    }
    return 0;
}

static void test_rules(void)
{
    printf("== the rules ==\n");
    static GameState g;
    Puzzle p;
    int i = find_puzzle(TH_MATE2, 4);
    check("a mate in 2 exists", i >= 0);
    if (i < 0) return;
    check("puzzle_start loads it", puzzle_start(&p, i, &g));
    check("with the opponent's move played", g.move_count - g.log_start == 1 && p.next == 1 && p.len == 4);
    Move wrong = other_move(&g, p.line[1]);
    U64 before = game_hash(&g);
    check("a wrong move is WRONG", wrong && puzzle_try(&p, &g, wrong) == PZ_WRONG);
    check("and leaves the position as it was", game_hash(&g) == before && p.failed);
    check("the solution move is RIGHT", puzzle_try(&p, &g, p.line[1]) == PZ_RIGHT);
    check("and the reply is played", p.next == 3 && g.move_count - g.log_start == 3);
    check("the last move SOLVES it", puzzle_try(&p, &g, p.line[3]) == PZ_SOLVED && p.done);

    puzzle_start(&p, i, &g);
    check("a hint names the piece to move", puzzle_hint(&p) == FROM(p.line[1]) && p.failed);
    int steps = 0;
    while (puzzle_show_step(&p, &g)) steps++;
    check("show plays the rest of the line", steps == 3 && p.done && g.move_count - g.log_start == 4);

    /* Any mate ends it, even one that is not the stored move. */
    int found = -1;
    Move alt = 0;
    for (int k = 0; k < puzzle_count && found < 0; k++) {
        if (!(puzzle_data[k].themes & TH_MATE1)) continue;
        puzzle_start(&p, k, &g);
        MoveList ml;
        generate_moves(&g.pos, &ml);
        for (int j = 0; j < ml.count; j++) {
            Position t = g.pos;
            if (ml.moves[j] != p.line[1] && make_move(&t, ml.moves[j]) && mates(&g, ml.moves[j])) {
                found = k;
                alt = ml.moves[j];
                break;
            }
        }
    }
    if (found < 0) printf("    (no mate in 1 with a second mate; skipped)\n");
    else check("another mating move also solves it", puzzle_try(&p, &g, alt) == PZ_SOLVED);

    /* A promotion, given as the cursor gives it: promo 0 means a queen. */
    int q = -1;
    for (int k = 0; k < puzzle_count && q < 0; k++) {
        if (!(puzzle_data[k].themes & TH_PROMOTION)) continue;
        puzzle_start(&p, k, &g);
        if (FLAGS(p.line[1]) & FLAG_PROMO_Q) q = k;
    }
    check("a queen-promotion puzzle exists", q >= 0);
    if (q >= 0) {
        Move m;
        game_find_move(&g, FROM(p.line[1]), TO(p.line[1]), 0, &m);
        check("its promotion, chosen without a piece, is accepted", puzzle_try(&p, &g, m) != PZ_WRONG);
    }
}

int main(void)
{
    init_attacks();
    test_data();
    test_rules();
    if (failures) {
        printf("\n%d puzzle test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll puzzle tests passed.\n");
    return 0;
}
