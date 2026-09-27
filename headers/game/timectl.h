#ifndef TIMECTL_H
#define TIMECTL_H

#include <stddef.h>

/* Base and increment per side (index WHITE/BLACK); base 0 = untimed. */
typedef struct { int base_ms[2], inc_ms[2]; } TimeControl;

/* "5+3" (minutes + seconds of increment), "0.5+0", "5+0/1+0" (White/Black),
 * or untimed/off/0. 1 on success. */
int  tc_parse(const char *s, TimeControl *out);
void tc_format(const TimeControl *tc, char *buf, size_t n);
int  tc_timed(const TimeControl *tc);
/* bullet, blitz, rapid, classical or untimed, from White's side. */
const char *tc_category(const TimeControl *tc);
/* How long an engine should think with `left_ms` on its clock. */
int  tc_budget_ms(long left_ms, int inc_ms);

#endif
