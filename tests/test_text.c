/* Display width of UTF-8 text.
 *
 * Build & run:  make test
 */
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include "utils/text.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

int main(void)
{
    if (!setlocale(LC_ALL, "C.UTF-8") && !setlocale(LC_ALL, "en_US.UTF-8")) {
        printf("  no UTF-8 locale; skipped\n");
        return 0;
    }
    printf("== width ==\n");
    check("ASCII is one column a byte", text_width("dchess") == 6);
    check("an accent is one column", text_width("Sämisch") == 7);
    check("CJK is two columns a character", text_width("测试") == 4);
    check("arrows are one column", text_width("↑↓") == 2);

    printf("== fit ==\n");
    check("fits whole ASCII", text_fit("dchess", 10) == 6);
    check("cuts ASCII at the width", text_fit("dchess", 3) == 3);
    check("never splits an accented letter", text_fit("Sämisch", 2) == 3);
    check("a wide character that does not fit is left out", text_fit("测试", 3) == 3);
    check("arrows cut between characters", text_fit("↑↓←→", 2) == 6);
    check("zero width fits nothing", text_fit("abc", 0) == 0);

    if (failures) {
        printf("\n%d text test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll text tests passed.\n");
    return 0;
}
