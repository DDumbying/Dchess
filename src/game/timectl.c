#include "game/timectl.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define BASE_MAX_MS (10 * 3600 * 1000)
#define INC_MAX_MS  (600 * 1000)

/* One side: "M+S", the whole string. */
static int parse_side(const char *s, const char *end, int *base, int *inc)
{
    char buf[32], *e;
    if (end - s <= 0 || end - s >= (long)sizeof(buf)) return 0;
    memcpy(buf, s, (size_t)(end - s));
    buf[end - s] = '\0';
    char *plus = strchr(buf, '+');
    if (!plus || plus == buf || !plus[1]) return 0;
    *plus = '\0';
    if (!isdigit((unsigned char)buf[0]) || !isdigit((unsigned char)plus[1])) return 0;
    double m = strtod(buf, &e);
    if (*e) return 0;
    double sec = strtod(plus + 1, &e);
    if (*e) return 0;
    double b = m * 60000.0, i = sec * 1000.0;
    if (b < 1000.0 || b > BASE_MAX_MS || i > INC_MAX_MS) return 0;
    *base = (int)(b + 0.5);
    *inc  = (int)(i + 0.5);
    return 1;
}

int tc_parse(const char *s, TimeControl *out)
{
    TimeControl t;
    memset(&t, 0, sizeof(t));
    if (!strcasecmp(s, "untimed") || !strcasecmp(s, "off") || !strcmp(s, "0")) {
        *out = t;
        return 1;
    }
    const char *slash = strchr(s, '/'), *end = s + strlen(s);
    if (!parse_side(s, slash ? slash : end, &t.base_ms[0], &t.inc_ms[0])) return 0;
    if (slash) {
        if (!parse_side(slash + 1, end, &t.base_ms[1], &t.inc_ms[1])) return 0;
    } else {
        t.base_ms[1] = t.base_ms[0];
        t.inc_ms[1]  = t.inc_ms[0];
    }
    *out = t;
    return 1;
}

int tc_timed(const TimeControl *tc) { return tc->base_ms[0] > 0; }

static void side_text(const TimeControl *tc, int side, char *buf, size_t n)
{
    snprintf(buf, n, "%g+%g", tc->base_ms[side] / 60000.0, tc->inc_ms[side] / 1000.0);
}

void tc_format(const TimeControl *tc, char *buf, size_t n)
{
    if (!tc_timed(tc)) { snprintf(buf, n, "untimed"); return; }
    char w[40], b[40];
    side_text(tc, 0, w, sizeof(w));
    side_text(tc, 1, b, sizeof(b));
    if (strcmp(w, b)) snprintf(buf, n, "%s/%s", w, b);
    else              snprintf(buf, n, "%s", w);
}

const char *tc_category(const TimeControl *tc)
{
    if (!tc_timed(tc)) return "untimed";
    long est = tc->base_ms[0] / 1000L + 40L * tc->inc_ms[0] / 1000L;   /* seconds */
    return est < 180 ? "bullet" : est < 600 ? "blitz" : est < 3600 ? "rapid" : "classical";
}

int tc_budget_ms(long left_ms, int inc_ms)
{
    if (left_ms < 50) return left_ms > 0 ? (int)(left_ms / 2) : 0;
    long b = left_ms / 30 + inc_ms * 4L / 5;
    if (b < 50) b = 50;
    if (b > left_ms / 3) b = left_ms / 3;
    return (int)b;
}
