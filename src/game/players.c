#include "game/players.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include <stdio.h>
#include <string.h>

Player player_human(void)
{
    Player p = { PLAYER_HUMAN, DIFF_MEDIUM, 0, 0 };
    return p;
}

Player player_builtin(int level)
{
    if (level < DIFF_EASY || level > DIFF_HARD) level = DIFF_MEDIUM;
    Player p = { PLAYER_BUILTIN, level,
                 cli_depth_for_difficulty(level),
                 cli_time_limit_for_difficulty(level) };
    return p;
}

int players_level_from_name(const char *name)
{
    if (strcmp(name, "easy") == 0)   return DIFF_EASY;
    if (strcmp(name, "medium") == 0) return DIFF_MEDIUM;
    if (strcmp(name, "hard") == 0)   return DIFF_HARD;
    return -1;
}

const char *players_level_name(int level)
{
    return level == DIFF_EASY ? "Easy" : level == DIFF_HARD ? "Hard" : "Medium";
}

static int humans(const Player p[2])
{
    return (p[WHITE].kind == PLAYER_HUMAN) + (p[BLACK].kind == PLAYER_HUMAN);
}

int players_automated(const Player p[2], int side)
{
    return p[side].kind != PLAYER_HUMAN;
}

int players_undo_plies(const Player p[2], int side_to_move, int undo_count)
{
    /* Against an engine, undo hands the turn back to the human. */
    int n = (humans(p) == 1 && !players_automated(p, side_to_move)) ? 2 : 1;
    return undo_count >= n ? n : 0;
}

int players_should_start(const Player p[2], int side_to_move, int paused,
                         int game_over, long since_last_move_ms)
{
    if (game_over || paused || !players_automated(p, side_to_move)) return 0;
    if (humans(p) == 0 && since_last_move_ms < PLAYERS_AUTOPLAY_DELAY_MS) return 0;
    return 1;
}

int players_stats_entry(const Player p[2], int *human_side, int *level)
{
    if (humans(p) != 1) return 0;
    int h = (p[WHITE].kind == PLAYER_HUMAN) ? WHITE : BLACK;
    int e = (h == WHITE) ? BLACK : WHITE;
    if (p[e].kind != PLAYER_BUILTIN) return 0;
    if (human_side) *human_side = h;
    if (level)      *level      = p[e].level;
    return 1;
}

int players_apply_command(Player p[2], const char *cmd, char *err, size_t n)
{
    if (strcmp(cmd, "swap") == 0) {
        Player t = p[WHITE];
        p[WHITE] = p[BLACK];
        p[BLACK] = t;
        return 1;
    }

    char colour[8], who[8], level[16], extra[2];
    int got = sscanf(cmd, "%7s %7s %15s %1s", colour, who, level, extra);
    if (got < 1) return 0;

    int side = strcmp(colour, "white") == 0 ? WHITE :
               strcmp(colour, "black") == 0 ? BLACK : -1;
    if (side < 0) return 0;

    const char *usage =
        "Use: white|black human, or white|black engine [easy|medium|hard]";

    if (got >= 2 && strcmp(who, "human") == 0) {
        if (got > 2) {
            snprintf(err, n, "A human has no level. %s", usage);
            return -1;
        }
        p[side] = player_human();
        return 1;
    }
    if (got >= 2 && got <= 3 && strcmp(who, "engine") == 0) {
        int lv = DIFF_MEDIUM;
        if (got == 3 && (lv = players_level_from_name(level)) < 0) {
            snprintf(err, n, "Unknown level '%s'. Use: easy | medium | hard", level);
            return -1;
        }
        p[side] = player_builtin(lv);
        return 1;
    }
    snprintf(err, n, "%s", usage);
    return -1;
}

void player_label(const Player *p, char *buf, size_t n)
{
    if (p->kind == PLAYER_HUMAN)
        snprintf(buf, n, "You");
    else
        snprintf(buf, n, "dchess %s", players_level_name(p->level));
}

/* Two humans are told apart by number rather than both being "You". */
static void side_name(const Player p[2], int side, char *buf, size_t n)
{
    if (humans(p) == 2)
        snprintf(buf, n, "Player %d", side == WHITE ? 1 : 2);
    else
        player_label(&p[side], buf, n);
}

void players_pgn_name(const Player p[2], int side, char *buf, size_t n)
{
    if (p[side].kind == PLAYER_BUILTIN)
        snprintf(buf, n, "dchess (%s)", players_level_name(p[side].level));
    else if (humans(p) == 2)
        snprintf(buf, n, "Player %d", side == WHITE ? 1 : 2);
    else
        snprintf(buf, n, "Player");
}

void players_matchup(const Player p[2], char *buf, size_t n)
{
    char w[32], b[32];
    side_name(p, WHITE, w, sizeof(w));
    side_name(p, BLACK, b, sizeof(b));
    snprintf(buf, n, "%s vs %s", w, b);
}

void players_describe(const Player p[2], char *buf, size_t n)
{
    char w[32], b[32];
    side_name(p, WHITE, w, sizeof(w));
    side_name(p, BLACK, b, sizeof(b));
    snprintf(buf, n, "White: %s · Black: %s", w, b);
}
