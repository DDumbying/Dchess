/* Theme tests: every colour combination the board and panels draw must
 * be legible, measured as WCAG contrast.
 *
 * Build & run:  make test
 */
#include <stdio.h>
#include <string.h>
#include "utils/theme.h"
#include "utils/cli.h"
#include "tui/colors.h"

static int failures = 0;
static int theme_failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void need(const Theme *t, const char *what, int a, int b, double min)
{
    double c = theme_contrast(a, b);
    if (c < min) {
        printf("        %s: %s is %.2f, needs %.2f\n", t->name, what, c, min);
        theme_failures++;
    }
}

static void test_registry(void)
{
    printf("== themes ==\n");
    check("four themes", theme_count() == 4);
    check("gruvbox is the default", strcmp(theme_name(0), "gruvbox") == 0);
    check("names are case-insensitive", theme_from_name("GruvBox") == 0);
    for (int i = 0; i < theme_count(); i++)
        if (theme_from_name(theme_name(i)) != i)
            check("a theme name does not round-trip", 0);
    check("every colour pair ID fits a 64-pair terminal", CP_LAST < 64);
}

static void test_contrast(void)
{
    printf("== contrast ==\n");
    check("black on white is the maximum, 21:1",
          theme_contrast(16, 231) > 20.9 && theme_contrast(16, 231) < 21.1);
    check("a colour against itself is 1:1", theme_contrast(100, 100) == 1.0);

    for (int i = 0; i < theme_count(); i++) {
        const Theme *t = theme_get(i);
        theme_failures = 0;

        need(t, "text",           t->fg,     t->bg, 7.0);
        need(t, "dim text",       t->dim,    t->bg, 3.0);
        need(t, "borders",        t->line,   t->bg, 1.4);
        need(t, "bar track",      t->track,  t->bg, 1.1);
        need(t, "shadow",         t->shadow, t->bg, 1.15);
        need(t, "ok text",        t->ok,     t->bg, 3.0);
        need(t, "error text",     t->err,    t->bg, 3.0);

        int acc[5] = { t->acc_board, t->acc_eval, t->acc_clock,
                       t->acc_moves, t->acc_engine };
        for (int k = 0; k < 5; k++) need(t, "panel title", acc[k], t->bg, 3.0);
        for (int k = 0; k < THEME_RAMP; k++)
            need(t, "gradient step", t->ramp[k], t->bg, 3.0);

        need(t, "light vs dark square", t->sq_light, t->sq_dark, 1.5);

        int under[8] = { t->sq_light, t->sq_dark, t->cursor, t->sel,
                         t->movehi, t->check, t->lm_light, t->lm_dark };
        for (int k = 0; k < 8; k++) {
            need(t, "white piece on a square", t->pc_white, under[k], 2.8);
            need(t, "black piece on a square", t->pc_black, under[k], 2.8);
        }

        char label[64];
        snprintf(label, sizeof(label), "%s: every combination is legible", t->name);
        check(label, theme_failures == 0);
    }
}

static void test_cli_names(void)
{
    printf("== --theme ==\n");
    CliArgs a;

    char *ok[] = { "dchess", "--theme", "catppuccin", NULL };
    memset(&a, 0, sizeof(a));
    check("a new theme name is accepted",
          cli_parse(3, ok, &a) == 0 && a.theme == theme_from_name("catppuccin"));

    char *old[] = { "dchess", "--theme", "classic", NULL };
    memset(&a, 0, sizeof(a));
    check("an old theme name is rejected", cli_parse(3, old, &a) != 0);
    check("and the error lists the new names", strstr(a.error_msg, "gruvbox") != NULL);
}

int main(void)
{
    test_registry();
    test_contrast();
    test_cli_names();

    if (failures) {
        printf("\n%d theme test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll theme tests passed.\n");
    return 0;
}
