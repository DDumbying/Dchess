#include "game/records.h"
#include "game/pgn.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *KIND_NAMES[] = { "profile", "guest", "dchess", "engine" };

int records_path(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    char dir[512];
    if (!home || !home[0]) home = "/tmp";
    snprintf(dir, sizeof(dir), "%s/.local", home);               mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.local/share", home);         mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/.local/share/dchess", home);  mkdir(dir, 0755);
    snprintf(buf, n, "%s/.local/share/dchess/games.pgn", home);
    return 1;
}

const char *records_end_reason(const char *result)
{
    if (strstr(result, "Checkmate"))    return "checkmate";
    if (strstr(result, "Stalemate"))    return "stalemate";
    if (strstr(result, "Insufficient")) return "material";
    if (strstr(result, "50-move"))      return "fifty-move";
    if (strstr(result, "repetition"))   return "repetition";
    if (strstr(result, "resigns"))      return "resigned";
    return "";
}

static SideKind kind_of(const Player *p)
{
    if (p->kind == PLAYER_BUILTIN) return KIND_DCHESS;
    if (p->kind == PLAYER_UCI)     return KIND_ENGINE;
    return p->name[0] ? KIND_PROFILE : KIND_GUEST;
}

static void side_name(const Player p[2], int side, char *buf, size_t n)
{
    SideKind k = kind_of(&p[side]);
    if (k == KIND_PROFILE)
        snprintf(buf, n, "%s", p[side].name);
    else if (k == KIND_GUEST && kind_of(&p[side ^ BLACK]) == KIND_GUEST)
        snprintf(buf, n, "Player %d", side == WHITE ? 1 : 2);
    else if (k == KIND_GUEST)
        snprintf(buf, n, "Guest");
    else
        players_pgn_name(p, side, buf, n);
}

static void strength(const Player *p, const EngineList *e, char *buf, size_t n)
{
    buf[0] = '\0';
    if (p->kind == PLAYER_BUILTIN) {
        snprintf(buf, n, "%s", players_level_name(p->level));
    } else if (p->kind == PLAYER_UCI && e) {
        const EngineEntry *x = engines_find(e, p->name);
        if (x) engine_strength_label(x, buf, n);
    }
}

static void add_tag(PgnHeader *h, const char *k, const char *v)
{
    if (v[0] && h->extra_count < 16) {
        h->extra[h->extra_count][0] = k;
        h->extra[h->extra_count][1] = v;
        h->extra_count++;
    }
}

int records_append(const char *path, const GameState *g, const Player p[2],
                   const EngineList *engines)
{
    char w[PLAYER_NAME_MAX + 1], b[PLAYER_NAME_MAX + 1], ws[32], bs[32], tm[16], plies[16], secs[16];
    side_name(p, WHITE, w, sizeof(w));
    side_name(p, BLACK, b, sizeof(b));
    strength(&p[WHITE], engines, ws, sizeof(ws));
    strength(&p[BLACK], engines, bs, sizeof(bs));

    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(tm, sizeof(tm), "%H:%M:%S", &t);
    snprintf(plies, sizeof(plies), "%d", g->move_count - g->log_start);
    snprintf(secs, sizeof(secs), "%d", g->white_clock + g->black_clock);

    PgnHeader h = { .white = w, .black = b };
    add_tag(&h, "Time", tm);
    add_tag(&h, "WhiteKind", KIND_NAMES[kind_of(&p[WHITE])]);
    add_tag(&h, "BlackKind", KIND_NAMES[kind_of(&p[BLACK])]);
    add_tag(&h, "WhiteStrength", ws);
    add_tag(&h, "BlackStrength", bs);
    add_tag(&h, "EndReason", records_end_reason(g->result));
    add_tag(&h, "PlyCount", plies);
    add_tag(&h, "Seconds", secs);
    return pgn_append(g, &h, path) == 0;
}

int records_append_legacy(const char *path, const char *profile, long timestamp, int result)
{
    FILE *f = fopen(path, "a");
    if (!f) return 0;
    if (ftell(f) > 0) fputc('\n', f);

    time_t t = (time_t)timestamp;
    struct tm tm;
    char d[16], h[16];
    localtime_r(&t, &tm);
    strftime(d, sizeof(d), "%Y.%m.%d", &tm);
    strftime(h, sizeof(h), "%H:%M:%S", &tm);
    const char *res = result > 0 ? "1-0" : result < 0 ? "0-1" : "1/2-1/2";

    fprintf(f, "[Event \"Casual game\"]\n[Site \"dchess\"]\n[Date \"%s\"]\n[Round \"-\"]\n"
               "[White \"%s\"]\n[Black \"dchess\"]\n[Result \"%s\"]\n[Time \"%s\"]\n"
               "[WhiteKind \"profile\"]\n[BlackKind \"dchess\"]\n[EndReason \"legacy\"]\n"
               "[PlyCount \"0\"]\n[Seconds \"0\"]\n\n%s\n", d, profile, res, h, res);
    int ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

/* [Key "Value"] only; anything else is not a tag. */
static int parse_tag(const char *line, char *key, size_t kn, char *val, size_t vn)
{
    if (line[0] != '[') return 0;
    const char *sp = strchr(line, ' ');
    const char *q1 = sp ? strchr(sp, '"') : NULL;
    const char *q2 = strrchr(line, '"');
    if (!q1 || q2 <= q1 || q2[1] != ']') return 0;
    size_t klen = (size_t)(sp - line - 1), vlen = (size_t)(q2 - q1 - 1);
    if (klen == 0 || klen >= kn) return 0;
    if (vlen >= vn) vlen = vn - 1;
    memcpy(key, line + 1, klen);
    key[klen] = '\0';
    memcpy(val, q1 + 1, vlen);
    val[vlen] = '\0';
    return 1;
}

static SideKind kind_from(const char *v)
{
    for (int i = 0; i < 4; i++)
        if (strcmp(v, KIND_NAMES[i]) == 0) return (SideKind)i;
    return KIND_GUEST;
}

#define COPY(dst, v) snprintf(dst, sizeof(dst), "%.*s", (int)sizeof(dst) - 1, v)

static void set_field(Record *r, const char *k, const char *v, int *have)
{
    if      (!strcmp(k, "White"))         { COPY(r->white, v); *have |= 1; }
    else if (!strcmp(k, "Black"))         { COPY(r->black, v); *have |= 2; }
    else if (!strcmp(k, "Result")) {
        r->result = !strcmp(v, "1-0") ? 1 : !strcmp(v, "0-1") ? -1 : 0;
        *have |= 4;
    }
    else if (!strcmp(k, "Date"))          COPY(r->date, v);
    else if (!strcmp(k, "Time"))          COPY(r->time, v);
    else if (!strcmp(k, "WhiteKind"))     r->white_kind = kind_from(v);
    else if (!strcmp(k, "BlackKind"))     r->black_kind = kind_from(v);
    else if (!strcmp(k, "WhiteStrength")) COPY(r->white_strength, v);
    else if (!strcmp(k, "BlackStrength")) COPY(r->black_strength, v);
    else if (!strcmp(k, "EndReason"))     { COPY(r->end_reason, v); r->legacy = !strcmp(v, "legacy"); }
    else if (!strcmp(k, "PlyCount"))      r->plies = atoi(v);
    else if (!strcmp(k, "Seconds"))       r->seconds = atoi(v);
}

static void keep(RecordList *l, const Record *r, int have)
{
    if (have != 7) return;
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 64;
        Record *grown = realloc(l->r, (size_t)cap * sizeof(Record));
        if (!grown) return;
        l->r = grown;
        l->cap = cap;
    }
    l->r[l->count++] = *r;
}

int records_load(const char *path, RecordList *out)
{
    out->r = NULL;
    out->count = out->cap = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;

    char line[1024], k[32], v[256];
    Record cur;
    int have = 0, open = 0, in_tags = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!parse_tag(line, k, sizeof(k), v, sizeof(v))) {
            in_tags = 0;
            continue;
        }
        /* A game starts at its first tag, so a broken Event line cannot
         * fold one game into the one before. */
        int starts = !in_tags || !strcmp(k, "Event");
        in_tags = 1;
        if (starts) {
            if (open) keep(out, &cur, have);
            memset(&cur, 0, sizeof(cur));
            cur.white_kind = cur.black_kind = KIND_GUEST;
            have = 0;
            open = 1;
        }
        set_field(&cur, k, v, &have);
    }
    if (open) keep(out, &cur, have);
    fclose(f);
    return 1;
}

void records_free(RecordList *l)
{
    free(l->r);
    l->r = NULL;
    l->count = l->cap = 0;
}

#define MAX_TAGS 64

/* Writes one game's tag block, relabelling only profile-kind sides. */
static void flush_tags(FILE *out, char tags[][1024], int n, const char *old, const char *new_name)
{
    char k[32], v[256], wk[16] = "", bk[16] = "";
    for (int i = 0; i < n; i++)
        if (parse_tag(tags[i], k, sizeof(k), v, sizeof(v))) {
            if (!strcmp(k, "WhiteKind")) COPY(wk, v);
            if (!strcmp(k, "BlackKind")) COPY(bk, v);
        }
    for (int i = 0; i < n; i++) {
        int is_tag = parse_tag(tags[i], k, sizeof(k), v, sizeof(v));
        if (is_tag && !strcmp(v, old) &&
            ((!strcmp(k, "White") && !strcmp(wk, "profile")) ||
             (!strcmp(k, "Black") && !strcmp(bk, "profile"))))
            fprintf(out, "[%s \"%s\"]\n", k, new_name);
        else
            fprintf(out, "%s\n", tags[i]);
    }
}

int records_rename(const char *path, const char *old, const char *new_name)
{
    FILE *in = fopen(path, "r");
    if (!in) return 1;   /* no games, nothing to relabel */
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *out = fopen(tmp, "w");
    char (*tags)[1024] = malloc(MAX_TAGS * sizeof(*tags));
    if (!out || !tags) {
        fclose(in);
        if (out) fclose(out);
        free(tags);
        return 0;
    }

    char line[1024], k[32], v[256];
    int n = 0, cont = 0;
    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        int whole = len && line[len - 1] == '\n';
        /* Anything but a whole tag line is copied byte for byte, so long
         * movetext lines come through unsplit. */
        if (!cont && whole) {
            line[strcspn(line, "\r\n")] = '\0';
            if (parse_tag(line, k, sizeof(k), v, sizeof(v)) && n < MAX_TAGS) {
                memcpy(tags[n++], line, sizeof(line));
                continue;
            }
            flush_tags(out, tags, n, old, new_name);
            n = 0;
            fprintf(out, "%s\n", line);
            continue;
        }
        flush_tags(out, tags, n, old, new_name);
        n = 0;
        fputs(line, out);
        cont = !whole;
    }
    flush_tags(out, tags, n, old, new_name);
    free(tags);
    fclose(in);
    int ok = !ferror(out);
    if (fclose(out) != 0 || !ok) { remove(tmp); return 0; }
    return rename(tmp, path) == 0;
}

static int side_of(const Record *r, const char *who)
{
    if (r->white_kind == KIND_PROFILE && !strcmp(r->white, who)) return WHITE;
    if (r->black_kind == KIND_PROFILE && !strcmp(r->black, who)) return BLACK;
    return -1;
}

static int outcome(const Record *r, int side) { return side == WHITE ? r->result : -r->result; }

RecordTally records_tally(const RecordList *l, const char *who)
{
    RecordTally t = { 0, 0, 0, 0 };
    for (int i = 0; i < l->count; i++) {
        int side = side_of(&l->r[i], who);
        if (side < 0 || l->r[i].legacy) continue;
        int o = outcome(&l->r[i], side);
        t.games++;
        if (o > 0) t.wins++;
        else if (o < 0) t.losses++;
        else t.draws++;
    }
    return t;
}

int records_recent(const RecordList *l, const char *who, const Record **out, int max)
{
    int n = 0;
    for (int i = l->count - 1; i >= 0 && n < max; i--)
        if (side_of(&l->r[i], who) >= 0) out[n++] = &l->r[i];
    return n;
}

int records_winrate(const RecordList *l, const char *who, float *out, int max)
{
    int games = 0, wins = 0, total = 0;
    for (int i = 0; i < l->count; i++)
        if (side_of(&l->r[i], who) >= 0) total++;
    int skip = total > max ? total - max : 0, n = 0;
    for (int i = 0; i < l->count; i++) {
        int side = side_of(&l->r[i], who);
        if (side < 0) continue;
        games++;
        if (outcome(&l->r[i], side) > 0) wins++;
        if (games > skip) out[n++] = (float)wins / (float)games;
    }
    return n;
}

long records_time(const Record *r)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    if (sscanf(r->date, "%d.%d.%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return 0;
    sscanf(r->time, "%d:%d:%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    return (long)mktime(&tm);
}

void records_to_stats(const RecordList *l, const char *who, DchessStats *out)
{
    memset(out, 0, sizeof(*out));
    int total = 0;
    for (int i = 0; i < l->count; i++)
        if (side_of(&l->r[i], who) >= 0) total++;
    int skip = total > DCHESS_MAX_HISTORY ? total - DCHESS_MAX_HISTORY : 0, seen = 0;

    for (int i = 0; i < l->count; i++) {
        const Record *r = &l->r[i];
        int side = side_of(r, who);
        if (side < 0) continue;
        int o = outcome(r, side);

        if (seen++ >= skip) {
            GameRecord *h = &out->history[out->history_count++];
            h->timestamp = records_time(r);
            h->result = o;
        }
        if (r->legacy) continue;

        SideKind opp = side == WHITE ? r->black_kind : r->white_kind;
        if (opp == KIND_DCHESS) {
            const char *s = side == WHITE ? r->black_strength : r->white_strength;
            int lv = !strcmp(s, "Easy") ? DIFF_EASY : !strcmp(s, "Hard") ? DIFF_HARD : DIFF_MEDIUM;
            out->games_played[lv]++;
            if (o > 0) out->wins[lv]++;
            else if (o < 0) out->losses[lv]++;
            else out->draws[lv]++;
        }
        out->total_moves += r->plies;
        out->total_time_secs += r->seconds;
        if (r->plies > out->longest_game_moves) out->longest_game_moves = r->plies;
        if (side == WHITE) {
            out->played_as_white++;
            if (o > 0) out->wins_as_white++;
        } else {
            out->played_as_black++;
            if (o > 0) out->wins_as_black++;
        }
    }
}
