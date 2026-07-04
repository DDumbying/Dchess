#ifndef THEME_H
#define THEME_H

/* Color theme table. Deliberately has no ncurses dependency -- cli.c
 * needs to validate a --theme flag without pulling ncurses into a file
 * that otherwise builds and tests standalone. Only render.c actually
 * hands these RGB values to ncurses (via init_color()/init_pair() in
 * init_colors()). */

typedef struct {
    const char *name;
    int light[3], dark[3], bpfg[3], cursor[3], sel[3], movehi[3],
        check[3], gold[3], lmvl[3], lmvd[3], canvas[3], chrome[3];
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
