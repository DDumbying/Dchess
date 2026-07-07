#ifndef THEME_H
#define THEME_H

/* Color theme table. Deliberately has no ncurses dependency -- cli.c
 * needs to validate a --theme flag without pulling ncurses into a file
 * that otherwise builds and tests standalone. Only render.c actually
 * hands these RGB values to ncurses (via init_color()/init_pair() in
 * init_colors()). */

/* Plain 0-7 color numbers matching the standard curses COLOR_* values
 * (which are the same integers) -- named here so theme.c doesn't need
 * to pull in ncurses.h just to write 0..7 with a label on each. */
#define FB_BLACK   0
#define FB_RED     1
#define FB_GREEN   2
#define FB_YELLOW  3
#define FB_BLUE    4
#define FB_MAGENTA 5
#define FB_CYAN    6
#define FB_WHITE   7

typedef struct {
    const char *name;
    int light[3], dark[3], bpfg[3], cursor[3], sel[3], movehi[3],
        check[3], gold[3], lmvl[3], lmvd[3], canvas[3], chrome[3];

    /* Fallback palette for terminals that don't support can_change_color()
     * (no custom RGB, only the 8 standard ANSI colors) -- common enough
     * that a theme needs to look different here too, not just in the
     * full-color path. Board squares stay a fixed black/white regardless
     * of theme (changing that risks hurting piece readability, which
     * matters more than theming); only the accent colors vary. */
    int fb_accent;    /* border/title/hint/labels */
    int fb_cursor_bg;
    int fb_sel_bg;
    int fb_movehi_bg;
    int fb_check_bg;
} Theme;

/* Number of built-in themes. */
int theme_count(void);

/* Theme by index (out-of-range clamps to 0, "classic"). Never NULL. */
const Theme *theme_get(int theme);

/* Name of a theme by index, for display. Out-of-range clamps to 0. */
const char *theme_name(int theme);

/* Case-insensitive name -> index lookup. Returns -1 if no theme matches. */
int theme_from_name(const char *name);

#endif
