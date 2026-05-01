#ifndef TUI_H
#define TUI_H

#include "engine/board.h"
#include "engine/search.h"
#include <ncurses.h>

#define MAX_MOVE_HISTORY 256

typedef struct {
    Position pos;
    char     move_history[MAX_MOVE_HISTORY][8];
    int      move_count;
    char     status[256];
    char     last_cmd[64];
    int      engine_depth;
    int      engine_side;
    int      game_over;
    char     last_eval[32];

    /* Cursor & selection state (for arrow-key UI) */
    int      cursor_row;    /* 0-7, display row (rank 8 = row 0) */
    int      cursor_col;    /* 0-7, display col (file a = col 0)  */
    int      selected;      /* 1 = a square is selected           */
    int      sel_row;
    int      sel_col;
    int      highlight[8][8]; /* legal move destinations           */
} TUIState;

void tui_init(TUIState *state);
void tui_run(TUIState *state);
void tui_cleanup(void);

#endif
