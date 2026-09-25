#include "game/pgn.h"
#include "utils/constants.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdlib.h>

#define PGN_LINE_WIDTH 80

/* The result tag, derived from the verdict game_update_status() wrote.
 * An unfinished game is "*", which is what PGN uses for adjourned or
 * abandoned games. */
static const char *result_token(const GameState *g)
{
    if (!g->game_over) return "*";
    if (strstr(g->result, "White wins")) return "1-0";
    if (strstr(g->result, "Black wins")) return "0-1";
    return "1/2-1/2";
}

static void today(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y.%m.%d", &tm);
}

int pgn_default_path(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";

    char dir[512];
    snprintf(dir, sizeof(dir), "%s/.local", home);            mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.local/share", home);      mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.local/share/dchess", home); mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.local/share/dchess/games", home);
    mkdir(dir, 0755);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);

    snprintf(buf, n, "%s/%s.pgn", dir, stamp);
    return 0;
}

/* Movetext wraps at PGN_LINE_WIDTH, never mid-token. */
static void write_movetext(FILE *f, const GameState *g)
{
    int col = 0;

    for (int i = g->log_start; i < g->move_count; i++) {
        char token[32];
        int n = 0;

        if (i % 2 == 0)
            n = snprintf(token, sizeof(token), "%d. %s", i / 2 + 1, g->move_history[i]);
        else if (i == g->log_start)   /* game begins with Black to move */
            n = snprintf(token, sizeof(token), "%d... %s", i / 2 + 1, g->move_history[i]);
        else
            n = snprintf(token, sizeof(token), "%s", g->move_history[i]);

        if (col && col + 1 + n > PGN_LINE_WIDTH) {
            fputc('\n', f);
            col = 0;
        } else if (col) {
            fputc(' ', f);
            col++;
        }
        fputs(token, f);
        col += n;
    }

    const char *res = result_token(g);
    if (col && col + 1 + (int)strlen(res) > PGN_LINE_WIDTH) fputc('\n', f);
    else if (col) fputc(' ', f);
    fputs(res, f);
    fputc('\n', f);
}

int pgn_write(const GameState *g, const PgnHeader *h, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    char date[16];
    today(date, sizeof(date));

    fprintf(f, "[Event \"%s\"]\n",  h && h->event ? h->event : "Casual game");
    fprintf(f, "[Site \"%s\"]\n",   h && h->site  ? h->site  : "dchess");
    fprintf(f, "[Date \"%s\"]\n",   date);
    fprintf(f, "[Round \"-\"]\n");
    fprintf(f, "[White \"%s\"]\n",  h && h->white ? h->white : "White");
    fprintf(f, "[Black \"%s\"]\n",  h && h->black ? h->black : "Black");
    fprintf(f, "[Result \"%s\"]\n", result_token(g));

    /* A game that did not start from the standard position is unreadable
     * without these two. */
    if (g->start_fen[0]) {
        fprintf(f, "[SetUp \"1\"]\n");
        fprintf(f, "[FEN \"%s\"]\n", g->start_fen);
    }

    fputc('\n', f);
    write_movetext(f, g);

    int ok = (ferror(f) == 0);
    if (fclose(f) != 0 || !ok) return -2;
    return 0;
}
