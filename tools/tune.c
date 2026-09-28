/* Texel tuning of the evaluation's added terms.
 *
 *   make tune   (reads build/fens.txt from make genfens)
 *
 * Fits the sigmoid scale K, then moves each parameter while the training
 * error falls, and stops when a pass changes nothing or the held-out games'
 * error has risen twice; the best parameters on those games are written to
 * build/tuned_params.h (and .txt, for match --cand-params). Copy the header
 * to src/engine/ once a match says it does not lose. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tune_core.h"
#include "utils/bitboard.h"

int main(int argc, char **argv)
{
    const char *in = "build/fens.txt", *out = "build/tuned_params.h", *terms = NULL;
    int threads = 14, max_passes = 200;
    for (int i = 1; i + 1 < argc; i += 2) {
        if      (!strcmp(argv[i], "--in"))      in = argv[i + 1];
        else if (!strcmp(argv[i], "--out"))     out = argv[i + 1];
        else if (!strcmp(argv[i], "--threads")) threads = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--passes"))  max_passes = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--terms"))   terms = argv[i + 1];
        else { fprintf(stderr, "unknown flag %s\n", argv[i]); return 2; }
    }
    init_attacks();
    TuneSet train = { 0 }, valid = { 0 };
    if (!tune_load(in, &train, &valid) || !train.n) { fprintf(stderr, "no positions in %s\n", in); return 1; }
    eval_set_params(eval_default_params());
    if (terms) {
        /* Tune only what will be played: those terms, and their weights. */
        EvalOptions o = { strstr(terms, "pesto") != NULL, strstr(terms, "pawns") != NULL,
                          strstr(terms, "mob") != NULL, strstr(terms, "king") != NULL,
                          strstr(terms, "xtra") != NULL };
        unsigned char m[EP_COUNT] = { 0 };
        for (int i = 0; i < EP_COUNT; i++)
            m[i] = (o.pawns && i < EP_MOB_TYPICAL) || (o.mobility && i >= EP_MOB_MG && i < EP_SHIELD) ||
                   (o.king && (i == EP_SHIELD || i == EP_OPEN_FILE)) || (o.extras && i >= EP_PAIR_MG);
        tune_set_terms(&o);
        tune_set_mask(m);
    }
    double K = tune_fit_k(&train, threads);
    double v0 = tune_error(&valid, K, threads), t0 = tune_error(&train, K, threads);
    fprintf(stderr, "%d training, %d held-out positions; K %.3f; error %.6f / %.6f\n",
            train.n, valid.n, K, t0, v0);

    EvalParams best = *eval_params();
    double best_valid = v0;
    int worse = 0;
    for (int pass = 1; pass <= max_passes; pass++) {
        double terr;
        int moved = tune_pass(&train, K, threads, &terr);
        double verr = tune_error(&valid, K, threads);
        fprintf(stderr, "pass %d: %d moved, error %.6f / %.6f\n", pass, moved, terr, verr);
        if (verr < best_valid) { best_valid = verr; best = *eval_params(); worse = 0; }
        else if (++worse >= 2) break;
        if (!moved) break;
    }
    tune_write_header(out, &best);
    char txt[512];
    snprintf(txt, sizeof(txt), "%s.txt", out);
    tune_write_params(txt, &best);
    printf("K %.3f; held-out error %.6f -> %.6f; written to %s\n", K, v0, best_valid, out);
    return 0;
}
