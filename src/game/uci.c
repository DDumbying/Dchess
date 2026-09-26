#include "game/uci.h"
#include "game/opponent_impl.h"
#include "engine/make.h"
#include "engine/move.h"
#include "engine/movegen.h"
#include "utils/constants.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

static long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void nap(long ms)
{
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* A write to an engine that just died must be an error, not a crash. */
static void ignore_sigpipe(void)
{
    static int done;
    if (!done) {
        signal(SIGPIPE, SIG_IGN);
        done = 1;
    }
}

/* The engine's stderr goes nowhere, so it cannot draw over the screen. */
static int spawn(const char *path, pid_t *pid, int *to_fd, int *from_fd)
{
    int in[2], out[2];
    if (pipe(in) != 0) return 0;
    if (pipe(out) != 0) {
        close(in[0]);
        close(in[1]);
        return 0;
    }
    int fds[4] = { in[0], in[1], out[0], out[1] };
    for (int i = 0; i < 4; i++) fcntl(fds[i], F_SETFD, FD_CLOEXEC);

    pid_t p = fork();
    if (p < 0) {
        for (int i = 0; i < 4; i++) close(fds[i]);
        return 0;
    }
    if (p == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) {
            dup2(null, 2);
            if (null > 2) close(null);
        }
        char *argv[] = { (char *)path, NULL };
        execvp(path, argv);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    fcntl(out[0], F_SETFL, fcntl(out[0], F_GETFL) | O_NONBLOCK);
    *pid = p;
    *to_fd = in[1];
    *from_fd = out[0];
    return 1;
}

static void reap(pid_t pid, int grace_ms)
{
    long until = now_ms() + grace_ms;
    while (now_ms() < until) {
        if (waitpid(pid, NULL, WNOHANG) == pid) return;
        nap(10);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

typedef enum { U_OFF, U_WAIT_UCIOK, U_WAIT_READY, U_IDLE, U_SEARCHING } UciState;

typedef struct {
    Opponent     base;
    EngineEntry  entry;

    pid_t        pid;          /* 0 while no engine is running */
    int          to_fd, from_fd;
    char         line[4096];
    int          len;
    int          skipping;     /* dropping the rest of an over-long line */

    UciState     state;
    long         deadline_ms;
    int          elo_supported, elo_min, elo_max;

    int          busy;         /* from start until the poll that returns */
    int          pending;      /* a search waiting for the handshake */
    int          done;         /* a result is ready for poll */
    SearchResult result;
    U64          key;
    Position     pos;          /* the position being searched */
    char         command[UCI_COMMAND_MAX];
    int          fresh;        /* send ucinewgame first */
    int          searched;
    char         last_fen[FEN_BUFSIZE];
    int          last_count;
    UciInfo      info;
    long         started_ms;
    char         error[400];
} Uci;

static void shut(Uci *u, int polite)
{
    if (!u->pid) return;
    if (polite && write(u->to_fd, "quit\n", 5) < 0) {}
    close(u->to_fd);
    reap(u->pid, polite ? 500 : 0);
    close(u->from_fd);
    u->pid = 0;
    u->state = U_OFF;
    u->len = 0;
    u->skipping = 0;
}

static void fail(Uci *u, const char *what)
{
    snprintf(u->error, sizeof(u->error), "%s: %s", u->entry.name, what);
    shut(u, 0);
    if (u->busy) {
        memset(&u->result, 0, sizeof(u->result));
        u->pending = 0;
        u->done = 1;
    }
}

static void gone(Uci *u)
{
    char what[320];
    if (u->state == U_WAIT_UCIOK || u->state == U_WAIT_READY)
        snprintf(what, sizeof(what), "could not start %s", u->entry.path);
    else
        snprintf(what, sizeof(what), "engine exited");
    fail(u, what);
}

static int say(Uci *u, const char *s)
{
    char buf[UCI_COMMAND_MAX + 2];
    size_t len = strlen(s), off = 0;
    if (len > UCI_COMMAND_MAX) len = UCI_COMMAND_MAX;
    memcpy(buf, s, len);
    buf[len++] = '\n';
    while (off < len) {
        ssize_t w = write(u->to_fd, buf + off, len - off);
        if (w < 0 && errno == EINTR) continue;
        if (w < 0) {
            gone(u);
            return 0;
        }
        off += (size_t)w;
    }
    return 1;
}

static void send_search(Uci *u)
{
    char go[48];
    if (u->fresh && !say(u, "ucinewgame")) return;
    if (!say(u, u->command)) return;
    if (u->entry.limit_depth) snprintf(go, sizeof(go), "go depth %d", u->entry.limit_depth);
    else                      snprintf(go, sizeof(go), "go movetime %d", u->entry.limit_ms);
    if (!say(u, go)) return;
    u->state = U_SEARCHING;
    u->pending = 0;
    u->searched = 1;
    u->started_ms = now_ms();
}

static void after_uciok(Uci *u)
{
    if (u->entry.elo && u->elo_supported) {
        int elo = u->entry.elo;
        if (elo < u->elo_min) elo = u->elo_min;
        if (u->elo_max && elo > u->elo_max) elo = u->elo_max;
        char opt[64];
        snprintf(opt, sizeof(opt), "setoption name UCI_Elo value %d", elo);
        if (!say(u, "setoption name UCI_LimitStrength value true")) return;
        if (!say(u, opt)) return;
    }
    if (!say(u, "isready")) return;
    u->state = U_WAIT_READY;
    u->deadline_ms = now_ms() + UCI_HANDSHAKE_TIMEOUT_MS;
}

static int legal_move(const Position *pos, const char *s, Move *out)
{
    int from, to, promo;
    if (!parse_move_str(s, &from, &to, &promo)) return 0;
    MoveList ml;
    generate_moves(pos, &ml);
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;
        if ((FLAGS(m) & FLAG_PROMOTION) && !(FLAGS(m) & (promo ? promo : FLAG_PROMO_Q)))
            continue;
        Position t = *pos;
        if (make_move(&t, m)) {
            *out = m;
            return 1;
        }
    }
    return 0;
}

static void finish(Uci *u, const char *move)
{
    Move m = 0;
    u->state = U_IDLE;
    /* An empty move is only honest when there is nothing to play. */
    if ((move[0] || has_legal_moves(&u->pos)) && !legal_move(&u->pos, move, &m)) {
        char what[64];
        snprintf(what, sizeof(what), "played illegal move %s", move[0] ? move : "(none)");
        fail(u, what);
        return;
    }
    memset(&u->result, 0, sizeof(u->result));
    u->result.best_move     = m;
    u->result.best_score    = uci_info_score(&u->info);
    u->result.depth_reached = u->info.depth;
    u->result.nodes         = u->info.nodes;
    u->result.elapsed_ms    = now_ms() - u->started_ms;
    u->done = 1;
}

static void merge(UciInfo *into, const UciInfo *from)
{
    if (from->depth) into->depth = from->depth;
    if (from->nodes) into->nodes = from->nodes;
    if (from->nps)   into->nps   = from->nps;
    if (from->has_score) {
        into->has_score = 1;
        into->score_cp  = from->score_cp;
        into->is_mate   = from->is_mate;
        into->mate_in   = from->mate_in;
    }
}

static void on_line(Uci *u, const char *line)
{
    char name[64], move[16];
    int lo, hi;
    UciInfo info;

    switch (u->state) {
    case U_WAIT_UCIOK:
        if (uci_parse_option(line, name, sizeof(name), &lo, &hi)) {
            if (strcmp(name, "UCI_Elo") == 0) {
                u->elo_supported = 1;
                u->elo_min = lo;
                u->elo_max = hi;
            }
        } else if (strcmp(line, "uciok") == 0) {
            after_uciok(u);
        }
        break;
    case U_WAIT_READY:
        if (strcmp(line, "readyok") == 0) {
            u->state = U_IDLE;
            if (u->pending) send_search(u);
        }
        break;
    case U_SEARCHING:
        if (uci_parse_info(line, &info))
            merge(&u->info, &info);
        else if (uci_parse_bestmove(line, move, sizeof(move)))
            finish(u, move);
        break;
    default:
        break;
    }
}

static void feed(Uci *u, const char *p, ssize_t n)
{
    for (ssize_t i = 0; i < n && u->pid; i++) {
        if (p[i] == '\n') {
            if (!u->skipping) {
                while (u->len && (u->line[u->len - 1] == '\r' || u->line[u->len - 1] == ' '))
                    u->len--;
                u->line[u->len] = '\0';
                on_line(u, u->line);
            }
            u->len = 0;
            u->skipping = 0;
        } else if (!u->skipping) {
            if (u->len < (int)sizeof(u->line) - 1) {
                u->line[u->len++] = p[i];
            } else {
                /* Too long: act on what fits, drop the rest. */
                u->line[u->len] = '\0';
                on_line(u, u->line);
                u->len = 0;
                u->skipping = 1;
            }
        }
    }
}

static void pump(Uci *u)
{
    char chunk[1024];
    while (u->pid) {
        ssize_t r = read(u->from_fd, chunk, sizeof(chunk));
        if (r > 0) { feed(u, chunk, r); continue; }
        if (r == 0) { gone(u); return; }
        if (errno != EINTR) return;
    }
}

static void check_deadline(Uci *u)
{
    if ((u->state == U_WAIT_UCIOK || u->state == U_WAIT_READY) && now_ms() > u->deadline_ms)
        fail(u, "no reply from engine");
}

static int uci_start(Opponent *o, const GameState *g)
{
    Uci *u = (Uci *)o;
    if (u->busy) return 0;

    u->error[0] = '\0';
    u->busy = 1;
    u->done = 0;
    u->pending = 1;
    memset(&u->info, 0, sizeof(u->info));
    u->pos = g->pos;
    u->key = game_hash(g);
    uci_position_command(g, u->command, sizeof(u->command));
    u->fresh = !u->searched || g->move_count < u->last_count ||
               strcmp(g->start_fen, u->last_fen) != 0;
    u->last_count = g->move_count;
    snprintf(u->last_fen, sizeof(u->last_fen), "%s", g->start_fen);

    if (!u->pid) {
        u->searched = 0;
        u->fresh = 1;
        u->elo_supported = 0;
        if (!spawn(u->entry.path, &u->pid, &u->to_fd, &u->from_fd)) {
            char what[320];
            snprintf(what, sizeof(what), "could not start %s", u->entry.path);
            fail(u, what);
            return 1;
        }
        u->state = U_WAIT_UCIOK;
        u->deadline_ms = now_ms() + UCI_HANDSHAKE_TIMEOUT_MS;
        say(u, "uci");
        return 1;
    }
    if (u->state == U_IDLE) send_search(u);
    return 1;
}

static int uci_poll(Opponent *o, SearchResult *out, U64 *key)
{
    Uci *u = (Uci *)o;
    if (!u->busy) return 0;
    pump(u);
    check_deadline(u);
    if (!u->done) return 0;
    if (out) *out = u->result;
    if (key) *key = u->key;
    u->busy = 0;
    u->done = 0;
    return 1;
}

static void uci_stop(Opponent *o)
{
    Uci *u = (Uci *)o;
    if (!u->busy || u->done) return;
    /* A search still waiting on the handshake has nothing to stop yet. */
    long until = now_ms() + UCI_STOP_TIMEOUT_MS;
    while (u->pid && u->pending && u->state != U_SEARCHING && now_ms() < until) {
        struct pollfd pf = { u->from_fd, POLLIN, 0 };
        poll(&pf, 1, 20);
        pump(u);
        check_deadline(u);
    }
    if (u->done) return;
    if (u->state != U_SEARCHING) {
        /* The search never began: it stops with no move. */
        u->pending = 0;
        memset(&u->result, 0, sizeof(u->result));
        u->done = 1;
        return;
    }
    if (!say(u, "stop")) return;
    until = now_ms() + UCI_STOP_TIMEOUT_MS;
    while (u->pid && u->state == U_SEARCHING && now_ms() < until) {
        struct pollfd pf = { u->from_fd, POLLIN, 0 };
        poll(&pf, 1, 20);
        pump(u);
    }
    if (u->state == U_SEARCHING) fail(u, "did not stop");
}

static void uci_cancel(Opponent *o)
{
    Uci *u = (Uci *)o;
    if (!u->busy) return;
    uci_stop(o);
    u->busy = 0;
    u->done = 0;
}

static void uci_destroy(Opponent *o)
{
    Uci *u = (Uci *)o;
    shut(u, 1);
    free(u);
}

static const char *uci_error(const Opponent *o)
{
    const Uci *u = (const Uci *)o;
    return u->error[0] ? u->error : NULL;
}

static const OpponentOps uci_ops = {
    uci_start, uci_poll, uci_stop, uci_cancel, uci_destroy, uci_error,
};

Opponent *opponent_uci(const EngineEntry *e)
{
    Uci *u = calloc(1, sizeof(*u));
    if (!u) return NULL;
    ignore_sigpipe();
    u->base.ops = &uci_ops;
    u->entry = *e;
    return &u->base;
}

static void probe_line(const char *line, UciProbe *p, int *ok)
{
    char name[64];
    int lo, hi;
    if (strncmp(line, "id name ", 8) == 0)
        snprintf(p->name, sizeof(p->name), "%.63s", line + 8);
    else if (strncmp(line, "id author ", 10) == 0)
        snprintf(p->author, sizeof(p->author), "%.63s", line + 10);
    else if (uci_parse_option(line, name, sizeof(name), &lo, &hi) && strcmp(name, "UCI_Elo") == 0) {
        p->elo_supported = 1;
        p->elo_min = lo;
        p->elo_max = hi;
    } else if (strcmp(line, "uciok") == 0)
        *ok = 1;
}

int uci_probe(const char *path, UciProbe *out, char *err, size_t n)
{
    pid_t pid;
    int to, from, ok = 0, eof = 0, len = 0;
    char line[4096], chunk[512];

    memset(out, 0, sizeof(*out));
    ignore_sigpipe();
    if (!spawn(path, &pid, &to, &from)) {
        snprintf(err, n, "could not start %s", path);
        return 0;
    }

    if (write(to, "uci\n", 4) != 4) eof = 1;
    long until = now_ms() + UCI_PROBE_TIMEOUT_MS;
    while (!ok && !eof && now_ms() < until) {
        struct pollfd pf = { from, POLLIN, 0 };
        poll(&pf, 1, 20);
        ssize_t r = -1;
        while (!ok && (r = read(from, chunk, sizeof(chunk))) > 0) {
            for (ssize_t i = 0; i < r && !ok; i++) {
                if (chunk[i] != '\n') {
                    if (len < (int)sizeof(line) - 1) line[len++] = chunk[i];
                    continue;
                }
                while (len && (line[len - 1] == '\r' || line[len - 1] == ' ')) len--;
                line[len] = '\0';
                len = 0;
                probe_line(line, out, &ok);
            }
        }
        if (!ok && r == 0) eof = 1;
    }

    if (write(to, "quit\n", 5) < 0) {}
    close(to);
    reap(pid, 500);
    close(from);

    if (ok) return 1;
    if (eof) snprintf(err, n, "could not start %s", path);
    else     snprintf(err, n, "no reply from engine");
    return 0;
}
