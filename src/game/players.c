#include "game/players.h"
#include "utils/cli.h"
#include "utils/constants.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

Player player_human(void)
{
    Player p = { PLAYER_HUMAN, DIFF_MEDIUM, 0, 0, "" };
    return p;
}

Player player_builtin(int level)
{
    if (level < DIFF_EASY || level > DIFF_HARD) level = DIFF_MEDIUM;
    Player p = { PLAYER_BUILTIN, level,
                 cli_depth_for_difficulty(level),
                 cli_time_limit_for_difficulty(level), "" };
    return p;
}

Player player_profile(const char *name)
{
    Player p = player_human();
    snprintf(p.name, sizeof(p.name), "%s", name);
    return p;
}

Player player_uci(const char *name)
{
    Player p = { PLAYER_UCI, DIFF_MEDIUM, 0, 0, "" };
    snprintf(p.name, sizeof(p.name), "%s", name);
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

static int both_guests(const Player p[2])
{
    return humans(p) == 2 && !p[WHITE].name[0] && !p[BLACK].name[0];
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

int players_apply_command(Player p[2], const char *cmd, char *err, size_t n,
                          const EngineList *engines,
                          const char *const *names, int count)
{
    char line[128];
    snprintf(line, sizeof(line), "%s", cmd);
    size_t len = strlen(line);
    while (len && line[len - 1] == ' ') line[--len] = '\0';

    if (strcmp(line, "swap") == 0) {
        Player t = p[WHITE];
        p[WHITE] = p[BLACK];
        p[BLACK] = t;
        return 1;
    }

    int side = strncmp(line, "white", 5) == 0 ? WHITE :
               strncmp(line, "black", 5) == 0 ? BLACK : -1;
    if (side < 0 || (line[5] != '\0' && line[5] != ' ')) return 0;

    const char *usage =
        "Use: white|black human [name|guest], or white|black engine [easy|medium|hard|name]";
    const char *rest = line + 5;
    while (*rest == ' ') rest++;

    if (strcmp(rest, "human") == 0) {
        p[side] = count > 0 ? player_profile(names[0]) : player_human();
        return 1;
    }
    if (strncmp(rest, "human ", 6) == 0) {
        const char *arg = rest + 6;
        while (*arg == ' ') arg++;
        if (strcasecmp(arg, "guest") == 0) {
            p[side] = player_human();
            return 1;
        }
        for (int i = 0; i < count; i++)
            if (strcmp(names[i], arg) == 0) {
                p[side] = player_profile(arg);
                return 1;
            }
        char list[160] = "";
        for (int i = 0; i < count; i++) {
            if (i) strncat(list, ", ", sizeof(list) - strlen(list) - 1);
            strncat(list, names[i], sizeof(list) - strlen(list) - 1);
        }
        snprintf(err, n, "Unknown profile '%s'. Profiles: %s", arg, count ? list : "none");
        return -1;
    }
    if (strcmp(rest, "engine") == 0) {
        p[side] = player_builtin(DIFF_MEDIUM);
        return 1;
    }
    if (strncmp(rest, "engine ", 7) == 0) {
        const char *arg = rest + 7;
        while (*arg == ' ') arg++;
        int lv = players_level_from_name(arg);
        if (lv >= 0) {
            p[side] = player_builtin(lv);
            return 1;
        }
        if (engines && engines_find(engines, arg)) {
            p[side] = player_uci(arg);
            return 1;
        }
        snprintf(err, n, "Unknown engine '%s'. Use easy, medium, hard or a name from 'engines'", arg);
        return -1;
    }
    snprintf(err, n, "%s", usage);
    return -1;
}

void player_label(const Player *p, char *buf, size_t n)
{
    if (p->kind == PLAYER_HUMAN)
        snprintf(buf, n, "%s", p->name[0] ? p->name : "Guest");
    else if (p->kind == PLAYER_UCI)
        snprintf(buf, n, "%s", p->name);
    else
        snprintf(buf, n, "dchess %s", players_level_name(p->level));
}

void player_word(const Player *p, char *buf, size_t n)
{
    if (p->kind == PLAYER_BUILTIN)
        snprintf(buf, n, "%s", p->level == DIFF_EASY ? "easy" : p->level == DIFF_HARD ? "hard" : "medium");
    else if (p->name[0])
        snprintf(buf, n, "%s", p->name);
    else
        snprintf(buf, n, "guest");
}

/* Two humans are told apart by number rather than both being "You". */
static void side_name(const Player p[2], int side, char *buf, size_t n)
{
    if (both_guests(p))
        snprintf(buf, n, "Player %d", side == WHITE ? 1 : 2);
    else
        player_label(&p[side], buf, n);
}

void players_pgn_name(const Player p[2], int side, char *buf, size_t n)
{
    if (p[side].kind == PLAYER_UCI)
        snprintf(buf, n, "%s", p[side].name);
    else if (p[side].kind == PLAYER_BUILTIN)
        snprintf(buf, n, "dchess (%s)", players_level_name(p[side].level));
    else if (both_guests(p))
        snprintf(buf, n, "Player %d", side == WHITE ? 1 : 2);
    else
        player_label(&p[side], buf, n);
}

void players_matchup(const Player p[2], char *buf, size_t n)
{
    char w[PLAYER_NAME_MAX + 1], b[PLAYER_NAME_MAX + 1];
    side_name(p, WHITE, w, sizeof(w));
    side_name(p, BLACK, b, sizeof(b));
    snprintf(buf, n, "%s vs %s", w, b);
}

void players_describe(const Player p[2], char *buf, size_t n)
{
    char w[PLAYER_NAME_MAX + 1], b[PLAYER_NAME_MAX + 1];
    side_name(p, WHITE, w, sizeof(w));
    side_name(p, BLACK, b, sizeof(b));
    snprintf(buf, n, "White: %s · Black: %s", w, b);
}
