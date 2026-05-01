#ifndef TUI_H
#define TUI_H

#include "engine/board.h"
#include "engine/search.h"
#include <ncurses.h>

#define TUI_BOARD_WIN_W  30
#define TUI_INFO_WIN_W   28
#define TUI_CMD_WIN_H    3
#define MAX_MOVE_HISTORY 128

typedef struct {
    Position pos;
    char     move_history[MAX_MOVE_HISTORY][8];
    int      move_count;
    char     status[256];
    char     last_cmd[64];
    int      engine_depth;
    int      engine_side;   /* which side engine plays: WHITE, BLACK, or -1 for human vs human */
    int      game_over;
    char     last_eval[32];
} TUIState;

void tui_init(TUIState *state);
void tui_run(TUIState *state);
void tui_cleanup(void);

#endif
