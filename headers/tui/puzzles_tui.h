#ifndef PUZZLES_TUI_H
#define PUZZLES_TUI_H

#include "tui/tui.h"
#include "game/puzzles.h"

typedef enum { PM_RATED, PM_THEMES, PM_RUSH, PM_MISSED } PuzzleMode;
typedef enum { PV_SOLVING, PV_RIGHT, PV_WRONG, PV_SOLVED, PV_SHOWN, PV_OVER } PuzzleVerdictShown;

/* What the panels show while a puzzle is on the board. */
typedef struct PuzzleView {
    PuzzleMode mode;
    unsigned   theme;
    Puzzle     pz;
    int        rating, delta, streak, rated;   /* rated: this result counted */
    int        score, strikes, left;           /* Rush; left: missed ones to go */
    int        missed_at;                      /* Missed: the place in the list */
    long       rush_left_ms, advance_at;       /* Rush: when the next puzzle comes */
    int        verdict, hint_sq, wrong_from, wrong_to;
} PuzzleView;

/* The puzzles menu and its modes; returns when the player leaves. */
void puzzles_screen(TUIState *s);

#endif
