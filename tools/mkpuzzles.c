/* The bundled puzzle set, from the Lichess puzzle database (CC0).
 *
 *   make puzzles CSV=lichess_db_puzzle.csv
 *
 * Keeps popular, well-rated puzzles of at most PUZZLE_PLIES plies (looser
 * limits only where a bucket runs short), spread
 * over 100-point buckets from 600 to 2899, each checked with dchess's own
 * move generator, and writes them sorted by rating as C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/puzzles.h"
#include "engine/move.h"
#include "utils/bitboard.h"

#define LO_BUCKET 6
#define HI_BUCKET 28
#define BUCKET_CAP 30000

typedef struct { char id[8], fen[96], moves[64]; int rating, popularity, plays, themes, relaxed; } Cand;

static Cand *bucket[HI_BUCKET + 1];
static int   nb[HI_BUCKET + 1];

static int theme_bits(char *themes)
{
    static const struct { const char *word; int bit; } map[] = {
        { "mateIn1", TH_MATE1 }, { "mateIn2", TH_MATE2 }, { "mateIn3", TH_MATE3 },
        { "fork", TH_FORK }, { "pin", TH_PIN }, { "skewer", TH_SKEWER },
        { "discoveredAttack", TH_DISCOVERED }, { "hangingPiece", TH_HANGING },
        { "sacrifice", TH_SACRIFICE }, { "endgame", TH_ENDGAME }, { "promotion", TH_PROMOTION },
        { "backRankMate", TH_BACKRANK }, { "defensiveMove", TH_DEFENSIVE },
    };
    int bits = 0, mate = 0;
    char *save = NULL;
    for (char *t = strtok_r(themes, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
        if (!strcmp(t, "mate")) mate = 1;
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            if (!strcmp(t, map[i].word)) bits |= map[i].bit;
    }
    if (mate && !(bits & (TH_MATE1 | TH_MATE2 | TH_MATE3))) bits |= TH_MATE;
    return bits;
}

/* The line plays legally, and a mate puzzle ends in mate. */
static int valid(const Cand *c)
{
    static GameState g;
    memset(&g, 0, sizeof(g));
    game_reset(&g);
    if (!game_load_fen(&g, c->fen)) return 0;
    char buf[64], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", c->moves);
    int n = 0;
    for (char *t = strtok_r(buf, " ", &save); t; t = strtok_r(NULL, " ", &save), n++) {
        int from, to, promo;
        Move m;
        if (!parse_move_str(t, &from, &to, &promo) || !game_find_move(&g, from, to, promo, &m)) return 0;
        game_play(&g, m);
    }
    game_update_status(&g);
    if (n < 2 || n > PUZZLE_PLIES) return 0;
    return !(c->themes & TH_ANY_MATE) || (g.game_over && !strncmp(g.result, "Checkmate", 9));
}

static int by_popularity(const void *a, const void *b)
{
    const Cand *x = a, *y = b;
    if (x->relaxed != y->relaxed) return x->relaxed - y->relaxed;
    if (x->popularity != y->popularity) return y->popularity - x->popularity;
    return y->plays - x->plays;
}

static int by_rating(const void *a, const void *b)
{
    const Cand *x = a, *y = b;
    return x->rating != y->rating ? x->rating - y->rating : strcmp(x->id, y->id);
}

int main(int argc, char **argv)
{
    const char *csv = NULL, *out = "src/game/puzzles_data.c";
    int per_bucket = 140;
    for (int i = 1; i + 1 < argc; i += 2) {
        if      (!strcmp(argv[i], "--csv"))        csv = argv[i + 1];
        else if (!strcmp(argv[i], "--out"))        out = argv[i + 1];
        else if (!strcmp(argv[i], "--per-bucket")) per_bucket = atoi(argv[i + 1]);
        else { fprintf(stderr, "unknown flag %s\n", argv[i]); return 2; }
    }
    if (!csv) { fprintf(stderr, "usage: mkpuzzles --csv FILE [--out FILE] [--per-bucket N]\n"); return 2; }
    FILE *f = fopen(csv, "r");
    if (!f) { perror(csv); return 1; }
    init_attacks();
    for (int b = LO_BUCKET; b <= HI_BUCKET; b++) bucket[b] = calloc(BUCKET_CAP, sizeof(Cand));

    char line[1024];
    long read = 0;
    while (fgets(line, sizeof(line), f)) {
        char *field[8], *p = line;
        int n = 0;
        /* PuzzleId,FEN,Moves,Rating,RatingDeviation,Popularity,NbPlays,Themes,… */
        for (char *t = strsep(&p, ","); t && n < 8; t = strsep(&p, ",")) field[n++] = t;
        if (n < 8 || !strcmp(field[0], "PuzzleId")) continue;
        read++;
        int rating = atoi(field[3]), dev = atoi(field[4]), pop = atoi(field[5]), plays = atoi(field[6]);
        int b = rating / 100;
        /* Strict: popular, much played, a settled rating. Relaxed fills the thin ends. */
        int strict = pop >= 90 && plays >= 1000 && dev < 80;
        int relaxed = pop >= 80 && plays >= 200 && dev < 100;
        if (!relaxed || b < LO_BUCKET || b > HI_BUCKET || nb[b] >= BUCKET_CAP) continue;
        if (strlen(field[0]) >= 8 || strlen(field[1]) >= 96 || strlen(field[2]) >= 64) continue;
        Cand c = { .rating = rating, .popularity = pop, .plays = plays, .relaxed = !strict };
        snprintf(c.id, sizeof(c.id), "%s", field[0]);
        snprintf(c.fen, sizeof(c.fen), "%s", field[1]);
        snprintf(c.moves, sizeof(c.moves), "%s", field[2]);
        c.themes = theme_bits(field[7]);
        if (valid(&c)) bucket[b][nb[b]++] = c;
    }
    fclose(f);

    Cand *keep = malloc(sizeof(Cand) * (HI_BUCKET + 1) * per_bucket);
    int kept = 0;
    for (int b = LO_BUCKET; b <= HI_BUCKET; b++) {
        qsort(bucket[b], nb[b], sizeof(Cand), by_popularity);
        char *taken = calloc(nb[b] ? nb[b] : 1, 1);
        int got = 0, per_theme[TH_BITS] = { 0 };
        /* Every theme first, up to 10 each, then the most popular. */
        for (int t = 0; t < TH_BITS; t++)
            for (int i = 0; i < nb[b] && per_theme[t] < 10 && got < per_bucket; i++)
                if (!taken[i] && (bucket[b][i].themes & (1 << t))) {
                    taken[i] = 1;
                    got++;
                    for (int u = 0; u < TH_BITS; u++) if (bucket[b][i].themes & (1 << u)) per_theme[u]++;
                    keep[kept++] = bucket[b][i];
                }
        for (int i = 0; i < nb[b] && got < per_bucket; i++)
            if (!taken[i]) { taken[i] = 1; got++; keep[kept++] = bucket[b][i]; }
        free(taken);
        int loose = 0;
        for (int i = kept - got; i < kept; i++) loose += keep[i].relaxed;
        fprintf(stderr, "%d00s: %d (%d relaxed) of %d\n", b, got, loose, nb[b]);
    }
    qsort(keep, kept, sizeof(Cand), by_rating);

    FILE *o = fopen(out, "w");
    if (!o) { perror(out); return 1; }
    fprintf(o, "/* Written by tools/mkpuzzles.c from the Lichess puzzle database\n"
               " * (database.lichess.org, CC0). Regenerate: make puzzles CSV=... */\n"
               "#include \"game/puzzles.h\"\n\nconst PuzzleData puzzle_data[] = {\n");
    for (int i = 0; i < kept; i++)
        fprintf(o, "    { \"%s\", \"%s\", \"%s\", %d, 0x%x },\n",
                keep[i].id, keep[i].fen, keep[i].moves, keep[i].rating, keep[i].themes);
    fprintf(o, "};\nconst int puzzle_count = %d;\n", kept);
    fclose(o);
    printf("%ld puzzles read, %d written to %s\n", read, kept, out);
    return 0;
}
