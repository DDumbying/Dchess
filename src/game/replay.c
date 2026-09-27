#include "game/replay.h"
#include "game/records.h"
#include "game/san.h"
#include "engine/make.h"
#include "engine/movegen.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define REPLAY_MAX_BYTES (64L * 1024 * 1024)
#define REPLAY_MAX_GAMES 10000

static FILE *open_pgn(const char *path, char *err, size_t n)
{
    struct stat st;
    if (stat(path, &st) != 0) { snprintf(err, n, "cannot open %s", path); return NULL; }
    if (!S_ISREG(st.st_mode)) { snprintf(err, n, "%s is not a file", path); return NULL; }
    if (st.st_size > REPLAY_MAX_BYTES) { snprintf(err, n, "%s is over 64 MiB", path); return NULL; }
    FILE *f = fopen(path, "rb");
    if (!f) snprintf(err, n, "cannot open %s", path);
    return f;
}

/* One line without its ending or a leading BOM; NULL at end of file. */
static char *next_line(FILE *f, char **buf, size_t *cap, long *pos)
{
    *pos = ftell(f);
    size_t len = 0;
    for (;;) {
        if (*cap - len < 256) {
            size_t c = *cap ? *cap * 2 : 1024;
            char *b = realloc(*buf, c);
            if (!b) return NULL;
            *buf = b;
            *cap = c;
        }
        if (!fgets(*buf + len, (int)(*cap - len), f)) {
            if (!len) return NULL;
            break;
        }
        len += strlen(*buf + len);
        if (len && (*buf)[len - 1] == '\n') break;
    }
    char *s = *buf;
    if (!memcmp(s, "\xEF\xBB\xBF", 3)) s += 3;
    s[strcspn(s, "\r\n")] = '\0';
    return s;
}

static void set_info(ReplayEntry *e, const char *k, const char *v)
{
    if      (!strcmp(k, "White"))  snprintf(e->white, sizeof(e->white), "%s", v);
    else if (!strcmp(k, "Black"))  snprintf(e->black, sizeof(e->black), "%s", v);
    else if (!strcmp(k, "Result")) snprintf(e->result, sizeof(e->result), "%s", v);
    else if (!strcmp(k, "Date"))   snprintf(e->date, sizeof(e->date), "%s", v);
    else if (!strcmp(k, "Event"))  snprintf(e->event, sizeof(e->event), "%s", v);
}

int replay_list(const char *path, ReplayList *out, char *err, size_t n)
{
    memset(out, 0, sizeof(*out));
    FILE *f = open_pgn(path, err, n);
    if (!f) return 0;
    char *buf = NULL, *line, k[32], v[256];
    size_t cap = 0;
    long pos;
    int in_tags = 0;
    ReplayEntry *cur = NULL;
    while ((line = next_line(f, &buf, &cap, &pos))) {
        if (!records_parse_tag(line, k, sizeof(k), v, sizeof(v))) {
            in_tags = 0;
            continue;
        }
        if (!in_tags || !strcmp(k, "Event")) {
            if (out->count == REPLAY_MAX_GAMES) break;
            if (out->count == out->cap) {
                int c = out->cap ? out->cap * 2 : 32;
                ReplayEntry *e = realloc(out->e, (size_t)c * sizeof(*e));
                if (!e) break;
                out->e = e;
                out->cap = c;
            }
            cur = &out->e[out->count++];
            memset(cur, 0, sizeof(*cur));
            cur->offset = pos;
        }
        in_tags = 1;
        set_info(cur, k, v);
    }
    free(buf);
    fclose(f);
    return 1;
}

void replay_list_free(ReplayList *l)
{
    free(l->e);
    memset(l, 0, sizeof(*l));
}

/* SAN compared loosely: no check, mate, annotation or '=' marks, and
 * castling written with zeros or lowercase letters. */
static void normalise(const char *in, char *out, size_t n)
{
    size_t o = 0;
    int castle = (in[0] == '0' || in[0] == 'o' || in[0] == 'O') && in[1] == '-';
    for (const char *c = in; *c && o + 1 < n; c++) {
        if (strchr("+#!?=", *c)) continue;
        char ch = *c;
        if (castle && (ch == '0' || ch == 'o')) ch = 'O';
        out[o++] = ch;
    }
    out[o] = '\0';
}

/* The one legal move written `tok`, or 0. */
static Move find_san(const GameState *g, const char *tok)
{
    char want[16], have[SAN_MAXLEN + 4], san[SAN_MAXLEN + 4];
    normalise(tok, want, sizeof(want));
    MoveList ml;
    generate_moves(&g->pos, &ml);
    Move found = 0;
    int hits = 0;
    for (int i = 0; i < ml.count; i++) {
        Position test = g->pos;
        if (!make_move(&test, ml.moves[i])) continue;
        san_write(&g->pos, ml.moves[i], san);
        normalise(san, have, sizeof(have));
        if (!strcmp(have, want)) { found = ml.moves[i]; hits++; }
    }
    return hits == 1 ? found : 0;
}

static int is_result(const char *t)
{
    return !strcmp(t, "1-0") || !strcmp(t, "0-1") || !strcmp(t, "1/2-1/2") || !strcmp(t, "*");
}

static void play_movetext(const char *t, GameState *g, ReplayGame *out)
{
    int depth = 0;
    while (*t) {
        if (*t == '{') { t = strchr(t, '}'); if (!t) return; t++; continue; }
        if (*t == ';') { t = strchr(t, '\n'); if (!t) return; continue; }
        if (*t == '(') { depth++; t++; continue; }
        if (*t == ')') { if (depth) depth--; t++; continue; }
        if (isspace((unsigned char)*t) || depth) { t++; continue; }
        if (*t == '$') { t++; while (isdigit((unsigned char)*t)) t++; continue; }

        char tok[32];
        size_t len = strcspn(t, " \t\r\n{}();");
        snprintf(tok, sizeof(tok), "%.*s", (int)(len < sizeof(tok) ? len : sizeof(tok) - 1), t);
        t += len;
        char *m = tok;
        char *d = m;
        while (isdigit((unsigned char)*d)) d++;
        if (d > m && *d == '.') {              /* a move number: "12." or "12..." */
            while (*d == '.') d++;
            m = d;
        }
        if (!*m || !strcmp(m, "e.p.")) continue;
        if (is_result(m)) return;
        if (g->move_count >= MAX_MOVE_HISTORY) {
            snprintf(out->err, sizeof(out->err), "stopped at the %d-move limit", MAX_MOVE_HISTORY);
            return;
        }
        Move mv = find_san(g, m);
        if (!mv) goto bad;
        game_play(g, mv);
        game_update_status(g);
        out->moves[out->count++] = mv;
        continue;
bad:
        snprintf(out->err, sizeof(out->err), "stopped at move %d: %.40s", g->move_count / 2 + 1, m);
        return;
    }
}

int replay_read(const char *path, long offset, ReplayGame *out, char *err, size_t n)
{
    memset(out, 0, sizeof(*out));
    FILE *f = open_pgn(path, err, n);
    if (!f) return 0;
    if (fseek(f, offset, SEEK_SET) != 0) {
        snprintf(err, n, "cannot read %s", path);
        fclose(f);
        return 0;
    }
    out->info.offset = offset;
    char *buf = NULL, *line, k[32], v[256];
    size_t cap = 0, len = 0, room = 0;
    long pos;
    char *text = NULL;
    int tags = 1;
    while ((line = next_line(f, &buf, &cap, &pos))) {
        int tag = records_parse_tag(line, k, sizeof(k), v, sizeof(v));
        if (tags && tag) {
            set_info(&out->info, k, v);
            if (!strcmp(k, "FEN")) snprintf(out->fen, sizeof(out->fen), "%.*s", FEN_BUFSIZE - 1, v);
            continue;
        }
        if (!tags && tag) break;               /* the next game */
        tags = 0;
        size_t l = strlen(line);
        if (len + l + 2 > room) {
            size_t r = room ? room * 2 : 4096;
            while (r < len + l + 2) r *= 2;
            char *t = realloc(text, r);
            if (!t) break;
            text = t;
            room = r;
        }
        memcpy(text + len, line, l);
        len += l;
        text[len++] = '\n';
        text[len] = '\0';
    }
    free(buf);
    fclose(f);

    static GameState g;
    game_reset(&g);
    if (out->fen[0] && !game_load_fen(&g, out->fen)) {
        snprintf(out->err, sizeof(out->err), "bad FEN: %.60s", out->fen);
        out->fen[0] = '\0';
    } else if (text) {
        play_movetext(text, &g, out);
    }
    free(text);
    return 1;
}
