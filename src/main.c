#include "tui/tui.h"
#include "utils/bitboard.h"
#include <locale.h>
#include <stdio.h>

int main(void) {
    setlocale(LC_ALL, "");   /* required for ncurses unicode output */
    init_attacks();

    TUIState state;
    tui_init(&state);
    tui_run(&state);

    return 0;
}
