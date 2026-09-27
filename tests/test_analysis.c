/* Analysis: principal lines, analysers and move grading.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/analysis.h"
#include "engine/search.h"
#include "engine/fen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "utils/bitboard.h"
#include <stdlib.h>
#include <time.h>

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static Analysis cp(int v)   { Analysis a = { .score_cp = v }; return a; }
static Analysis mate(int n) { Analysis a = { .mate = n, .score_cp = n > 0 ? 30000 - n : -30000 - n }; return a; }

static void test_grades(void)
{
    printf("== grading ==\n");
    Analysis b = cp(100), a;
    a = cp(60);   check("40 cp lost is fine", review_grade(&b, &a, WHITE) == GRADE_NONE);
    a = cp(40);   check("60 cp is an inaccuracy", review_grade(&b, &a, WHITE) == GRADE_INACCURACY);
    a = cp(-50);  check("150 cp is a mistake", review_grade(&b, &a, WHITE) == GRADE_MISTAKE);
    a = cp(-250); check("350 cp is a blunder", review_grade(&b, &a, WHITE) == GRADE_BLUNDER);
    b = cp(-100); a = cp(250);
    check("Black's loss is White's gain", review_grade(&b, &a, BLACK) == GRADE_BLUNDER);
    b = cp(2000); a = cp(1500);
    check("scores are clamped at 1500", review_grade(&b, &a, WHITE) == GRADE_NONE);
    b = mate(3); a = mate(2);
    check("keeping a mate is fine", review_grade(&b, &a, WHITE) == GRADE_NONE);
    b = cp(50); a = mate(-2);
    check("allowing a mate is a blunder", review_grade(&b, &a, WHITE) == GRADE_BLUNDER);
    check("marks", !strcmp(grade_mark(GRADE_BLUNDER), "??") && !strcmp(grade_mark(GRADE_MISTAKE), "?") &&
                   !strcmp(grade_mark(GRADE_INACCURACY), "?!") && !strcmp(grade_mark(GRADE_NONE), ""));
}

static int legal_line(const Position *start, const Move *line, int n)
{
    Position p = *start;
    for (int i = 0; i < n; i++)
        if (!make_move(&p, line[i])) return 0;
    return 1;
}

static void test_pv(void)
{
    printf("== principal line ==\n");
    Position pos;
    parse_fen("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 2 3", &pos, NULL, NULL);
    Position s = pos;
    SearchResult r = search(&s, 4, 0);
    Move line[8];
    int n = search_pv(&pos, r.best_move, line, 8);
    check("a mate's line starts with the mating move", n >= 1 && line[0] == r.best_move);
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &pos, NULL, NULL);
    s = pos;
    r = search(&s, 5, 0);
    n = search_pv(&pos, r.best_move, line, 8);
    check("a quiet line runs several moves", n >= 2 && line[0] == r.best_move);
    check("and every move is legal in turn", legal_line(&pos, line, n));
}

static int wait_result(Analyser *a, Analysis *out, int ms)
{
    U64 key;
    struct timespec t = { 0, 10 * 1000000L };
    for (int i = 0; i < ms / 10; i++) {
        if (analyser_poll(a, out, &key)) return 1;
        nanosleep(&t, NULL);
    }
    return 0;
}

static void test_builtin(void)
{
    printf("== built-in analyser ==\n");
    static GameState g;
    game_reset(&g);
    Analyser *a = analyser_builtin(300);
    Analysis r;
    check("the start position is analysed", analyser_start(a, &g) && wait_result(a, &r, 3000));
    Move m;
    check("with a legal best move", r.best && game_find_move(&g, FROM(r.best), TO(r.best), 0, &m));
    check("and a line that starts with it", r.line_len >= 1 && r.depth >= 1);
    game_load_fen(&g, "7k/6Q1/6K1/8/8/8/8/8 b - - 0 1");
    game_update_status(&g);
    check("a finished game is not analysed", analyser_start(a, &g) == 0);
    analyser_free(a);
}

static void test_uci(void)
{
    printf("== UCI analyser ==\n");
    setenv("FAKE_UCI_MODE", "analyse", 1);
    EngineEntry e;
    memset(&e, 0, sizeof(e));
    snprintf(e.name, sizeof(e.name), "Fake");
    snprintf(e.path, sizeof(e.path), "build/fake_uci");
    e.limit_ms = 100;
    static GameState g;
    game_reset(&g);
    Analyser *a = analyser_uci(&e, 100);
    Analysis r;
    check("the engine's score is White's view", analyser_start(a, &g) && wait_result(a, &r, 5000) &&
                                                r.score_cp == 35);
    check("and its line is in SAN", r.line_len == 3 && !strcmp(r.line[0], "e4") && !strcmp(r.line[2], "Nf3"));
    int from, to, promo;
    Move m;
    parse_move_str("e2e4", &from, &to, &promo);
    game_find_move(&g, from, to, promo, &m);
    game_play(&g, m);
    game_update_status(&g);
    check("with Black to move the score flips", analyser_start(a, &g) && wait_result(a, &r, 5000) &&
                                                r.score_cp == -35);
    analyser_free(a);
    setenv("FAKE_UCI_MODE", "crash", 1);
    a = analyser_uci(&e, 100);
    game_reset(&g);
    check("a crashing engine reports an error", analyser_start(a, &g) && wait_result(a, &r, 5000) &&
                                                !r.best && analyser_error(a)[0]);
    analyser_free(a);
    unsetenv("FAKE_UCI_MODE");
}

int main(void)
{
    init_attacks();
    test_grades();
    test_pv();
    test_builtin();
    test_uci();
    if (failures) {
        printf("\n%d analysis test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll analysis tests passed.\n");
    return 0;
}
