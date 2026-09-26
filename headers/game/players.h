#ifndef PLAYERS_H
#define PLAYERS_H

#include <stddef.h>
#include "utils/engines.h"

typedef enum { PLAYER_HUMAN, PLAYER_BUILTIN, PLAYER_UCI } PlayerKind;

/* level, depth and time_ms are for the built-in engine; engine names a registry entry. */
typedef struct {
    PlayerKind kind;
    int        level;     /* DIFF_EASY / DIFF_MEDIUM / DIFF_HARD */
    int        depth;
    int        time_ms;
    char       engine[ENGINE_NAME_MAX + 1];
} Player;

/* Between two engines, so a person can follow the game. */
#define PLAYERS_AUTOPLAY_DELAY_MS 500

Player      player_human(void);
Player      player_builtin(int level);
Player      player_uci(const char *name);
int         players_level_from_name(const char *name);
const char *players_level_name(int level);

int  players_automated(const Player p[2], int side);

/* Plies "undo" takes back, or 0 when there is not enough history. */
int  players_undo_plies(const Player p[2], int side_to_move, int undo_count);

int  players_should_start(const Player p[2], int side_to_move, int paused,
                          int game_over, long since_last_move_ms);

/* 1 when exactly one side is human and the other is the built-in engine. */
int  players_stats_entry(const Player p[2], int *human_side, int *level);

/* 1 applied, 0 not a player command, -1 invalid with a message in err.
 * `engines` resolves names after "engine"; NULL resolves none. */
int  players_apply_command(Player p[2], const char *cmd, char *err, size_t n,
                           const EngineList *engines);

void player_label(const Player *p, char *buf, size_t n);
void players_pgn_name(const Player p[2], int side, char *buf, size_t n);
void players_matchup(const Player p[2], char *buf, size_t n);
void players_describe(const Player p[2], char *buf, size_t n);

#endif
