#include "game/analysis.h"

#define GRADE_CLAMP 1500

/* A score from the mover's side, clamped so won positions do not grade
 * every quiet move. */
static int mover_view(const Analysis *a, int mover)
{
    int v = a->mate ? (a->mate > 0 ? GRADE_CLAMP : -GRADE_CLAMP) : a->score_cp;
    if (v > GRADE_CLAMP) v = GRADE_CLAMP;
    if (v < -GRADE_CLAMP) v = -GRADE_CLAMP;
    return mover == WHITE ? v : -v;
}

Grade review_grade(const Analysis *before, const Analysis *after, int mover)
{
    int sign = mover == WHITE ? 1 : -1;
    if (before->mate * sign > 0 && after->mate * sign > 0) return GRADE_NONE;
    int loss = mover_view(before, mover) - mover_view(after, mover);
    return loss >= 300 ? GRADE_BLUNDER : loss >= 100 ? GRADE_MISTAKE
         : loss >= 50  ? GRADE_INACCURACY : GRADE_NONE;
}

const char *grade_mark(Grade g)
{
    static const char *marks[] = { "", "?!", "?", "??" };
    return marks[g];
}
