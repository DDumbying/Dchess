#include <string.h>
#include "game/puzzles.h"

static const char *const theme_names[TH_BITS] = {
    "mate in 1", "mate in 2", "mate in 3", "longer mate", "fork", "pin", "skewer",
    "discovered attack", "hanging piece", "sacrifice", "endgame", "promotion",
    "back-rank mate", "defence",
};

const char *puzzle_theme_name(int bit)
{
    for (int i = 0; i < TH_BITS; i++)
        if (bit == 1 << i) return theme_names[i];
    return "";
}

int puzzle_find(const char *id)
{
    for (int i = 0; i < puzzle_count; i++)
        if (!strcmp(puzzle_data[i].id, id)) return i;
    return -1;
}
