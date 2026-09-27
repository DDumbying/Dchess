#include "utils/text.h"
#include <string.h>
#include <wchar.h>

/* Width of the character at `s`; its length in bytes goes to *len. */
static int char_width(const char *s, mbstate_t *st, size_t *len)
{
    wchar_t wc;
    size_t n = mbrtowc(&wc, s, strlen(s), st);
    if (n == (size_t)-1 || n == (size_t)-2 || n == 0) {
        memset(st, 0, sizeof(*st));
        *len = 1;                   /* a stray byte shows as one cell */
        return 1;
    }
    *len = n;
    int w = wcwidth(wc);
    return w < 0 ? 1 : w;
}

int text_width(const char *s)
{
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    int cols = 0;
    size_t len;
    for (; *s; s += len) cols += char_width(s, &st, &len);
    return cols;
}

size_t text_fit(const char *s, int width)
{
    mbstate_t st;
    memset(&st, 0, sizeof(st));
    const char *p = s;
    int cols = 0;
    size_t len;
    while (*p) {
        int w = char_width(p, &st, &len);
        if (cols + w > width) break;
        cols += w;
        p += len;
    }
    return (size_t)(p - s);
}
