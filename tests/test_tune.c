/* The tuner's core: the sigmoid, the error, the K fit and a pass.
 *
 * Build & run:  make test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "tune_core.h"
#include "engine/fen.h"
#include "utils/bitboard.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

static void add(TuneSet *s, const char *fen, float result)
{
    Position pos;
    parse_fen(fen, &pos, NULL, NULL);
    tune_add(s, &pos, result, 1);
}

int main(void)
{
    init_attacks();
    printf("== sigmoid ==\n");
    check("an even score is a half", fabs(tune_sigmoid(0, 1.0) - 0.5) < 1e-12);
    check("400 at K 1 is ten to one", fabs(tune_sigmoid(400, 1.0) - 1.0 / 1.1) < 1e-12);

    printf("== error ==\n");
    TuneSet s = { 0 };
    add(&s, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 0.5f);
    add(&s, "4k3/8/8/8/8/8/8/3QK3 w - - 0 1", 1.0f);
    add(&s, "3qk3/8/8/8/8/8/8/4K3 b - - 0 1", 0.0f);
    EvalOptions all = { 1, 1, 1, 1, 1 };
    eval_set_options(&all);
    double want = 0;
    for (int i = 0; i < s.n; i++) {
        int e = evaluate(&s.p[i].pos);
        if (s.p[i].pos.side == BLACK) e = -e;
        double d = s.p[i].result - tune_sigmoid(e, 1.0);
        want += d * d;
    }
    want /= s.n;
    check("the error matches a hand computation", fabs(tune_error(&s, 1.0, 2) - want) < 1e-12);

    printf("== fitting ==\n");
    TuneSet w = { 0 };
    const char *fens[] = { "4k3/8/8/8/8/8/PPP5/4K3 w - - 0 1", "4k3/ppp5/8/8/8/8/8/4K3 w - - 0 1",
                           "4k3/8/8/8/8/8/8/R3K3 w - - 0 1", "r3k3/8/8/8/8/8/8/4K3 w - - 0 1",
                           "4k3/8/8/8/8/8/P7/4K3 w - - 0 1", "4k3/p7/8/8/8/8/8/4K3 w - - 0 1" };
    const float res[] = { 1, 0, 1, 0, 0.5f, 0.5f };
    for (int r = 0; r < 20; r++)
        for (int i = 0; i < 6; i++) add(&w, fens[i], res[i]);
    double K = tune_fit_k(&w, 2);
    check("K comes out in range", K > 0.1 && K < 3.0);
    double before = tune_error(&w, K, 2), after;
    tune_pass(&w, K, 2, &after);
    check("a pass never raises the error", after <= before + 1e-12);

    printf("== the tools' edges ==\n");
    EvalOptions o;
    unsigned char m[EP_COUNT];
    int ok = tune_parse_terms("pesto,king,mob", &o, m);
    check("terms are read by exact name", ok && o.pesto && o.king && o.mobility && !o.pawns && !o.extras &&
                                          m[EP_MOB_MG] && m[EP_SHIELD] && !m[EP_MOB_TYPICAL] && !m[EP_PAIR_MG]);
    check("unknown or loose names are refused", !tune_parse_terms("all", &o, m) &&
          !tune_parse_terms("-pawns", &o, m) && !tune_parse_terms("pesto,foo", &o, m) &&
          !tune_parse_terms("", &o, m));

    char path[] = "/tmp/dchess-tune-XXXXXX";
    int fd = mkstemp(path);
    FILE *f = fdopen(fd, "w");
    for (int i = 0; i <= EP_COUNT; i++) fprintf(f, "%d ", i);
    fclose(f);
    EvalParams rp;
    check("a params file with extra numbers is refused", !tune_read_params(path, &rp));

    f = fopen(path, "w");
    for (unsigned g = 1; g <= 40; g++)
        for (int k = 0; k < 3; k++) fprintf(f, "4k3/8/8/8/8/8/%dP%d/4K3 w - - 0 1;1;%u\n", k, 7 - k, g);
    fclose(f);
    TuneSet tr = { 0 }, va = { 0 };
    tune_load(path, &tr, &va);
    int split = tr.n && va.n;
    for (int i = 0; i < tr.n; i++)
        for (int j = 0; j < va.n; j++) if (tr.p[i].game == va.p[j].game) split = 0;
    check("no game is in both the training and held-out sets", split);
    unlink(path);

    /* Data that always wants a bigger king shield. */
    TuneSet k = { 0 };
    for (int r = 0; r < 10; r++) add(&k, "q5k1/8/8/8/8/8/5PPP/Q5K1 w - - 0 1", 1.0f);
    EvalOptions king = { 0, 0, 0, 1, 0 };
    unsigned char only[EP_COUNT] = { 0 };
    only[EP_SHIELD] = 1;
    tune_set_terms(&king);
    tune_set_mask(only);
    EvalParams hi = *eval_default_params();
    hi.v[EP_SHIELD] = TUNE_BOUND - 1;
    eval_set_params(&hi);
    tune_pass(&k, 1.0, 2, &after);
    check("no weight passes the bound", eval_params()->v[EP_SHIELD] <= TUNE_BOUND &&
                                        eval_params()->v[EP_SHIELD] >= TUNE_BOUND - 1);

    eval_set_params(eval_default_params());
    if (failures) {
        printf("\n%d tuning test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll tuning tests passed.\n");
    return 0;
}
