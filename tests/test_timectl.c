/* Time-control text and engine budgets.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "game/timectl.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static int round_trip(const char *in, const char *out)
{
    TimeControl tc;
    char buf[32];
    if (!tc_parse(in, &tc)) return 0;
    tc_format(&tc, buf, sizeof(buf));
    return !strcmp(buf, out);
}

int main(void)
{
    TimeControl tc;
    printf("== parsing ==\n");
    check("5+3", round_trip("5+3", "5+3") && tc_parse("5+3", &tc) &&
                 tc.base_ms[0] == 300000 && tc.inc_ms[1] == 3000);
    check("half a minute", round_trip("0.5+0", "0.5+0") && tc_parse("0.5+0", &tc) && tc.base_ms[0] == 30000);
    check("odds", round_trip("5+0/1+0", "5+0/1+0") && tc_parse("5+0/1+0", &tc) &&
                  tc.base_ms[0] == 300000 && tc.base_ms[1] == 60000);
    check("untimed spellings", round_trip("untimed", "untimed") && round_trip("off", "untimed") &&
                               round_trip("0", "untimed") && tc_parse("off", &tc) && !tc_timed(&tc));
    check("ten hours is the limit", tc_parse("600+0", &tc) && !tc_parse("601+0", &tc));
    const char *bad[] = { "abc", "5+", "-1+0", "0+5", "5+0/0", "5+601", "", "5+3x", "700+0" };
    int ok = 1;
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        if (tc_parse(bad[i], &tc)) { ok = 0; printf("    accepted %s\n", bad[i]); }
    check("nonsense is rejected", ok);

    printf("== categories ==\n");
    tc_parse("1+0", &tc);   check("1+0 is bullet", !strcmp(tc_category(&tc), "bullet"));
    tc_parse("3+2", &tc);   check("3+2 is blitz", !strcmp(tc_category(&tc), "blitz"));
    tc_parse("15+10", &tc); check("15+10 is rapid", !strcmp(tc_category(&tc), "rapid"));
    tc_parse("90+30", &tc); check("90+30 is classical", !strcmp(tc_category(&tc), "classical"));
    tc_parse("off", &tc);   check("untimed", !strcmp(tc_category(&tc), "untimed"));

    printf("== budgets ==\n");
    check("a thirtieth of the time", tc_budget_ms(300000, 0) == 10000);
    check("plus most of the increment", tc_budget_ms(300000, 3000) == 12400);
    check("at least 50 ms", tc_budget_ms(1000, 0) == 50);
    check("half of a nearly empty clock", tc_budget_ms(40, 0) == 20);
    check("never more than a third", tc_budget_ms(3000, 5000) == 1000);

    if (failures) {
        printf("\n%d time-control test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll time-control tests passed.\n");
    return 0;
}
