/* Dashboard layout and graph maths.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "utils/dash.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void test_side_width(void)
{
    printf("== side column width ==\n");
    check("hidden below 60 columns",  dash_side_width(59) == 0);
    check("26 wide at exactly 60",    dash_side_width(60) == 26);
    check("26 wide at 109",           dash_side_width(109) == 26);
    check("34 wide from 110",         dash_side_width(110) == 34);
    check("hidden at the 34-column minimum", dash_side_width(34) == 0);
}

static void test_side_layout(void)
{
    printf("== side column layout ==\n");

    DashSide tall = dash_side_layout(40);
    check("tall: every panel shown",
          tall.eval_h == 6 && tall.clock_h == 6 && tall.engine_h == 5);
    check("tall: moves take the rest", tall.moves_h == 40 - 17);

    DashSide mid = dash_side_layout(18);
    check("short: the eval graph shrinks first", mid.eval_h == 4);
    check("short: that alone is enough, engine stays", mid.engine_h == 5);
    check("short: moves keep their minimum", mid.moves_h == 3);

    DashSide shorter = dash_side_layout(17);
    check("shorter: the engine panel goes next", shorter.engine_h == 0);
    check("shorter: clocks survive", shorter.clock_h == 6);
    check("shorter: moves still have room", shorter.moves_h >= 3);

    for (int h = 0; h <= 60; h++) {
        DashSide s = dash_side_layout(h);
        int sum = s.eval_h + s.clock_h + s.moves_h + s.engine_h;
        if (sum > h || s.moves_h < 0) {
            check("no layout ever overflows its height", 0);
            return;
        }
    }
    check("no layout ever overflows its height", 1);
}

static void test_graph(void)
{
    printf("== eval graph ==\n");
    check("level is half height",        dash_graph_fill(0, 3) == 12);
    check("+5 pawns fills the column",   dash_graph_fill(500, 3) == 24);
    check("-5 pawns empties it",         dash_graph_fill(-500, 3) == 0);
    check("a mate score clamps to full", dash_graph_fill(999000, 3) == 24);
    check("a mated score clamps to empty", dash_graph_fill(-999000, 3) == 0);
    check("+1 pawn is above half",       dash_graph_fill(100, 3) > 12);
    check("no rows, no fill",            dash_graph_fill(300, 0) == 0);

    check("a short game starts at the beginning", dash_graph_start(5, 20) == 0);
    check("a long game shows the newest",         dash_graph_start(50, 20) == 30);
    check("an empty history starts at 0",         dash_graph_start(0, 20) == 0);
}

static void test_ramp_and_bars(void)
{
    printf("== gradient and bars ==\n");
    check("bottom level is the first colour",   dash_ramp_index(0, 3, 8) == 0);
    check("top level is the last colour",       dash_ramp_index(2, 3, 8) == 7);
    check("a single level uses the first colour", dash_ramp_index(0, 1, 8) == 0);
    check("out-of-range levels are clamped",    dash_ramp_index(9, 3, 8) == 7);

    check("half of the time fills half the bar", dash_bar_fill(50, 100, 20) == 10);
    check("all of it fills the bar",            dash_bar_fill(100, 100, 20) == 20);
    check("no time spent yet: empty bars",      dash_bar_fill(0, 0, 20) == 0);
    check("nothing spent by this side: empty",  dash_bar_fill(0, 100, 20) == 0);
}

static void test_count(void)
{
    printf("== counts ==\n");
    char b[16];
    dash_count(999, b, sizeof(b));     check("999 stays 999",  strcmp(b, "999") == 0);
    dash_count(1500, b, sizeof(b));    check("1500 is 1.5K",   strcmp(b, "1.5K") == 0);
    dash_count(2300000, b, sizeof(b)); check("2300000 is 2.3M", strcmp(b, "2.3M") == 0);
}

int main(void)
{
    test_side_width();
    test_side_layout();
    test_graph();
    test_ramp_and_bars();
    test_count();

    if (failures) {
        printf("\n%d dashboard test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll dashboard tests passed.\n");
    return 0;
}
