#include <stdio.h>
#include <string.h>
#include "game/puzzles.h"
#include "engine/move.h"

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

int puzzle_start(Puzzle *p, int index, GameState *g)
{
    memset(p, 0, sizeof(*p));
    if (index < 0 || index >= puzzle_count) return 0;
    p->index = index;
    memset(g, 0, sizeof(*g));
    game_reset(g);
    if (!game_load_fen(g, puzzle_data[index].fen)) return 0;
    static GameState scratch;
    scratch = *g;
    char buf[128], *save = NULL;
    snprintf(buf, sizeof(buf), "%s", puzzle_data[index].moves);
    for (char *t = strtok_r(buf, " ", &save); t && p->len < PUZZLE_PLIES; t = strtok_r(NULL, " ", &save)) {
        int from, to, promo;
        Move m;
        if (!parse_move_str(t, &from, &to, &promo) || !game_find_move(&scratch, from, to, promo, &m)) return 0;
        game_play(&scratch, m);
        p->line[p->len++] = m;
    }
    if (p->len < 2) return 0;
    game_play(g, p->line[0]);
    p->next = 1;
    return 1;
}

static int gives_mate(const GameState *g, Move m)
{
    static GameState t;
    t = *g;
    game_play(&t, m);
    game_update_status(&t);
    return t.game_over && !strncmp(t.result, "Checkmate", 9);
}

static int same_move(Move a, Move b)
{
    return FROM(a) == FROM(b) && TO(a) == TO(b) && (FLAGS(a) & FLAG_PROMOTION) == (FLAGS(b) & FLAG_PROMOTION);
}

PuzzleVerdict puzzle_try(Puzzle *p, GameState *g, Move m)
{
    if (p->done) return PZ_SOLVED;
    int mate = gives_mate(g, m);
    if (!mate && !same_move(m, p->line[p->next])) {
        p->failed = 1;
        return PZ_WRONG;
    }
    game_play(g, m);
    if (mate || p->next + 1 >= p->len) {
        p->done = 1;
        game_update_status(g);
        return PZ_SOLVED;
    }
    game_play(g, p->line[p->next + 1]);
    p->next += 2;
    return PZ_RIGHT;
}

int puzzle_hint(Puzzle *p)
{
    if (p->done) return -1;
    p->failed = 1;
    return FROM(p->line[p->next]);
}

int puzzle_show_step(Puzzle *p, GameState *g)
{
    if (p->done) return 0;
    p->failed = 1;
    game_play(g, p->line[p->next++]);
    if (p->next >= p->len) {
        p->done = 1;
        game_update_status(g);
    }
    return 1;
}
