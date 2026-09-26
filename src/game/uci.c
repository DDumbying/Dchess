#include "game/uci.h"
#include "engine/move.h"
#include "utils/constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UCI_MAX_TOKENS 256

/* Splits a copy of `line` on whitespace. */
static int tokenize(const char *line, char *copy, size_t cn, char **tok, int max)
{
    char *save;
    int n = 0;
    snprintf(copy, cn, "%s", line);
    for (char *t = strtok_r(copy, " \t\r\n", &save); t && n < max;
         t = strtok_r(NULL, " \t\r\n", &save))
        tok[n++] = t;
    return n;
}

int uci_parse_info(const char *line, UciInfo *out)
{
    char copy[4096], *tok[UCI_MAX_TOKENS];
    int n = tokenize(line, copy, sizeof(copy), tok, UCI_MAX_TOKENS);
    if (n == 0 || strcmp(tok[0], "info") != 0) return 0;

    memset(out, 0, sizeof(*out));
    for (int i = 1; i < n; i++) {
        const char *k = tok[i];
        /* Everything after these is moves or free text. */
        if (strcmp(k, "pv") == 0 || strcmp(k, "string") == 0) break;
        if (i + 1 >= n) break;
        if      (strcmp(k, "depth") == 0) out->depth = atoi(tok[++i]);
        else if (strcmp(k, "nodes") == 0) out->nodes = atol(tok[++i]);
        else if (strcmp(k, "nps") == 0)   out->nps   = atol(tok[++i]);
        else if (strcmp(k, "score") == 0 && i + 2 < n) {
            if (strcmp(tok[i + 1], "cp") == 0) {
                out->has_score = 1;
                out->score_cp  = atoi(tok[i + 2]);
            } else if (strcmp(tok[i + 1], "mate") == 0) {
                out->has_score = 1;
                out->is_mate   = 1;
                out->mate_in   = atoi(tok[i + 2]);
            }
            i += 2;
        }
    }
    return 1;
}

int uci_parse_bestmove(const char *line, char *move, size_t n)
{
    char copy[256], *tok[4];
    int c = tokenize(line, copy, sizeof(copy), tok, 4);
    if (c == 0 || strcmp(tok[0], "bestmove") != 0) return 0;
    if (c < 2 || strcmp(tok[1], "(none)") == 0) move[0] = '\0';
    else snprintf(move, n, "%s", tok[1]);
    return 1;
}

int uci_parse_option(const char *line, char *name, size_t n, int *min, int *max)
{
    char copy[1024], *tok[UCI_MAX_TOKENS];
    int c = tokenize(line, copy, sizeof(copy), tok, UCI_MAX_TOKENS);
    if (c < 3 || strcmp(tok[0], "option") != 0 || strcmp(tok[1], "name") != 0) return 0;

    name[0] = '\0';
    *min = *max = 0;
    int i = 2;
    for (; i < c && strcmp(tok[i], "type") != 0; i++) {
        if (name[0]) strncat(name, " ", n - strlen(name) - 1);
        strncat(name, tok[i], n - strlen(name) - 1);
    }
    for (; i + 1 < c; i++) {
        if (strcmp(tok[i], "min") == 0)      *min = atoi(tok[++i]);
        else if (strcmp(tok[i], "max") == 0) *max = atoi(tok[++i]);
    }
    return 1;
}

void uci_position_command(const GameState *g, char *buf, size_t n)
{
    if (g->start_fen[0]) snprintf(buf, n, "position fen %s", g->start_fen);
    else                 snprintf(buf, n, "position startpos");
    if (g->move_count > g->log_start)
        strncat(buf, " moves", n - strlen(buf) - 1);

    for (int i = g->log_start; i < g->move_count; i++) {
        char m[8] = " ";
        move_to_str(g->move_made[i], m + 1);
        if (strlen(buf) + strlen(m) + 1 > n) break;
        strcat(buf, m);
    }
}

int uci_info_score(const UciInfo *info)
{
    if (!info->has_score) return 0;
    if (!info->is_mate)   return info->score_cp;
    int m = info->mate_in < 0 ? -info->mate_in : info->mate_in;
    return info->mate_in < 0 ? -(MATE_SCORE - m) : MATE_SCORE - m;
}
