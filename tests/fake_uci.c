/* A scripted UCI engine for the tests. FAKE_UCI_MODE (or argv[1]) picks
 * how it behaves; FAKE_UCI_LOG, when set, gets every line it reads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine/board.h"
#include "engine/fen.h"
#include "engine/make.h"
#include "engine/move.h"
#include "engine/movegen.h"
#include "utils/bitboard.h"

static const char *START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
static Position pos;

static int legal(Move m)
{
    Position t = pos;
    return make_move(&t, m);
}

static int first_legal(Move *out)
{
    MoveList ml;
    generate_moves(&pos, &ml);
    for (int i = 0; i < ml.count; i++)
        if (legal(ml.moves[i])) { *out = ml.moves[i]; return 1; }
    return 0;
}

static void play(const char *s)
{
    int from, to, promo;
    if (!parse_move_str(s, &from, &to, &promo)) return;
    MoveList ml;
    generate_moves(&pos, &ml);
    for (int i = 0; i < ml.count; i++) {
        Move m = ml.moves[i];
        if (FROM(m) != from || TO(m) != to) continue;
        if (promo && !(FLAGS(m) & promo)) continue;
        if (legal(m)) { make_move(&pos, m); return; }
    }
}

/* "startpos|fen <6 fields> [moves ...]" */
static void set_position(char *args)
{
    char *save, *t = strtok_r(args, " ", &save);
    if (!t) return;
    if (strcmp(t, "startpos") == 0) {
        parse_fen(START, &pos, NULL, NULL);
    } else if (strcmp(t, "fen") == 0) {
        char fen[128] = "";
        for (int i = 0; i < 6 && (t = strtok_r(NULL, " ", &save)); i++) {
            if (i) strncat(fen, " ", sizeof(fen) - strlen(fen) - 1);
            strncat(fen, t, sizeof(fen) - strlen(fen) - 1);
        }
        parse_fen(fen, &pos, NULL, NULL);
    }
    while ((t = strtok_r(NULL, " ", &save)))
        if (strcmp(t, "moves") != 0) play(t);
}

static void say(const char *s)
{
    printf("%s\n", s);
    fflush(stdout);
}

static void bestmove(void)
{
    Move m;
    char buf[8], line[32];
    if (!first_legal(&m)) { say("bestmove (none)"); return; }
    move_to_str(m, buf);
    snprintf(line, sizeof(line), "bestmove %s", buf);
    say(line);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : getenv("FAKE_UCI_MODE");
    if (!mode) mode = "normal";
    const char *logpath = getenv("FAKE_UCI_LOG");
    FILE *log = logpath ? fopen(logpath, "a") : NULL;

    init_attacks();
    parse_fen(START, &pos, NULL, NULL);

    static char line[16384];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (log) { fprintf(log, "%s\n", line); fflush(log); }
        if (strcmp(line, "quit") == 0) break;
        if (strcmp(mode, "mute") == 0) continue;

        if (strcmp(line, "uci") == 0 && strcmp(mode, "flood") == 0) {
            /* Big writes, like yes(1), so the pipe never runs dry. */
            static char block[65536];
            for (size_t i = 0; i < sizeof(block); i++) block[i] = (i % 32 == 31) ? '\n' : 'f';
            for (;;) if (write(1, block, sizeof(block)) < 0) return 1;
        }
        if (strcmp(line, "uci") == 0) {
            say("id name Fake UCI");
            say("id author dchess tests");
            if (strcmp(mode, "elo") == 0) {
                say("option name UCI_LimitStrength type check default false");
                say("option name UCI_Elo type spin default 1320 min 1320 max 3190");
            }
            say("uciok");
        } else if (strcmp(line, "isready") == 0) {
            say("readyok");
        } else if (strncmp(line, "position ", 9) == 0) {
            set_position(line + 9);
        } else if (strncmp(line, "go", 2) == 0) {
            if (strcmp(mode, "crash") == 0) exit(3);
            if (strcmp(mode, "chatty") == 0) {
                static char big[10000];
                memset(big, 'x', sizeof(big) - 1);
                printf("info string %s\n", big);
                fflush(stdout);
            }
            say("info depth 1 score cp 12 nodes 20 nps 1000");
            if (strcmp(mode, "slow") == 0 || strcmp(mode, "deaf") == 0) continue;
            say("info depth 2 score cp 15 nodes 400 nps 2000");
            if (strcmp(mode, "illegal") == 0) say("bestmove e2e5");
            else bestmove();
        } else if (strcmp(line, "stop") == 0) {
            if (strcmp(mode, "slow") == 0) bestmove();
        }
    }
    if (log) fclose(log);
    return 0;
}
