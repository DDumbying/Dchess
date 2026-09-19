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

typedef struct {
    const char *name;
    int light[3], dark[3], bpfg[3], cursor[3], sel[3], movehi[3],
        check[3], gold[3], lmvl[3], lmvd[3], canvas[3], chrome[3];

    /* Mid-grey, not black: every canvas here is already near-black. */
    int shadow[3];

    /* For terminals without can_change_color(). Board squares stay a
     * fixed black/white -- readability beats theming -- so only the
     * accents vary. */
    int fb_accent;    /* border/title/hint/labels */
    int fb_cursor_bg;
    int fb_sel_bg;
    int fb_movehi_bg;
    int fb_check_bg;
} Theme;

int theme_count(void);

/* Out-of-range clamps to 0 ("classic"). Never NULL. */
const Theme *theme_get(int theme);
const char *theme_name(int theme);

/* Case-insensitive. Returns -1 if no theme matches. */
int theme_from_name(const char *name);

#endif
