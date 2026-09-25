#ifndef THEME_H
#define THEME_H

/* No ncurses dependency: cli.c validates --theme without pulling ncurses
 * into a file that otherwise builds standalone. Only render.c hands
 * these values to ncurses.
 *
 * FB_* are the standard curses COLOR_* integers, named here so theme.c
 * need not include ncurses.h just to write 0..7. */
#define FB_BLACK   0
#define FB_RED     1
#define FB_GREEN   2
#define FB_YELLOW  3
#define FB_BLUE    4
#define FB_MAGENTA 5
#define FB_CYAN    6
#define FB_WHITE   7

#define THEME_RAMP 8

/* All colours are xterm-256 palette indices, used directly: no
 * init_color(), so tmux and screen (which report can_change_color() false
 * while offering 256 colours) get the real theme. */
typedef struct {
    const char *name;
    int bg, fg, dim, line;
    int acc_board, acc_eval, acc_clock, acc_moves, acc_engine;
    int ramp[THEME_RAMP];            /* gradient, low to high */
    int track;                       /* empty part of a bar */
    int sq_light, sq_dark, pc_white, pc_black;
    int cursor, sel, movehi, check, lm_light, lm_dark;
    int ok, err, shadow;

    /* Terminals with fewer than 256 colours. */
    int fb_accent;
    int fb_cursor_bg;
    int fb_sel_bg;
    int fb_movehi_bg;
    int fb_check_bg;
} Theme;

int theme_count(void);

/* Out-of-range clamps to 0 ("gruvbox"). Never NULL. */
const Theme *theme_get(int theme);
const char *theme_name(int theme);

/* Case-insensitive. Returns -1 if no theme matches. */
int theme_from_name(const char *name);

/* 0xRRGGBB for an xterm-256 index, for direct-colour terminals. */
int theme_rgb(int index);

/* WCAG contrast ratio between two xterm-256 colours, 1.0 to 21.0. */
double theme_contrast(int a, int b);

#endif
