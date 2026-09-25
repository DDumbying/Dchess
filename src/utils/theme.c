#include "utils/theme.h"
#include <strings.h>
#include <math.h>

static const Theme THEMES[] = {
    { "gruvbox",
      235, 223, 245, 239,
      208, 142, 214, 175, 109,
      { 142, 142, 178, 214, 214, 208, 202, 167 }, 237,
      137, 94, 230, 234,
      66, 100, 65, 160, 136, 130,
      142, 167, 238,
      FB_YELLOW, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
    { "tokyonight",
      234, 189, 103, 238,
      111, 149, 179, 141, 117,
      { 149, 149, 150, 186, 179, 215, 210, 204 }, 236,
      103, 60, 231, 233,
      27, 64, 32, 204, 172, 130,
      149, 204, 237,
      FB_BLUE, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
    { "btop",
      16, 254, 245, 240,
      203, 83, 220, 75, 170,
      { 83, 83, 119, 190, 220, 214, 208, 203 }, 235,
      245, 241, 231, 16,
      25, 28, 30, 124, 136, 94,
      83, 203, 237,
      FB_GREEN, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
    { "catppuccin",
      235, 189, 103, 239,
      183, 151, 223, 218, 117,
      { 151, 151, 187, 223, 223, 217, 211, 211 }, 237,
      138, 96, 231, 234,
      31, 65, 32, 204, 172, 94,
      151, 211, 238,
      FB_MAGENTA, FB_CYAN, FB_GREEN, FB_BLUE, FB_RED },
};
#define THEME_COUNT ((int)(sizeof(THEMES) / sizeof(THEMES[0])))

int theme_count(void) { return THEME_COUNT; }

const Theme *theme_get(int theme)
{
    if (theme < 0 || theme >= THEME_COUNT) theme = 0;
    return &THEMES[theme];
}

const char *theme_name(int theme)
{
    return theme_get(theme)->name;
}

int theme_from_name(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < THEME_COUNT; i++)
        if (strcasecmp(name, THEMES[i].name) == 0) return i;
    return -1;
}

static void xterm_rgb(int i, int rgb[3])
{
    static const int SYS[16][3] = {
        {0,0,0},{128,0,0},{0,128,0},{128,128,0},{0,0,128},{128,0,128},
        {0,128,128},{192,192,192},{128,128,128},{255,0,0},{0,255,0},
        {255,255,0},{0,0,255},{255,0,255},{0,255,255},{255,255,255},
    };
    static const int CUBE[6] = { 0, 95, 135, 175, 215, 255 };

    if (i < 16) {
        rgb[0] = SYS[i][0]; rgb[1] = SYS[i][1]; rgb[2] = SYS[i][2];
    } else if (i < 232) {
        i -= 16;
        rgb[0] = CUBE[i / 36]; rgb[1] = CUBE[(i / 6) % 6]; rgb[2] = CUBE[i % 6];
    } else {
        rgb[0] = rgb[1] = rgb[2] = 8 + 10 * (i - 232);
    }
}

static double channel(int v)
{
    double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static double luminance(int i)
{
    int c[3];
    xterm_rgb(i, c);
    return 0.2126 * channel(c[0]) + 0.7152 * channel(c[1]) + 0.0722 * channel(c[2]);
}

double theme_contrast(int a, int b)
{
    double x = luminance(a), y = luminance(b);
    if (x < y) { double t = x; x = y; y = t; }
    return (x + 0.05) / (y + 0.05);
}

int theme_rgb(int index)
{
    int c[3];
    xterm_rgb(index, c);
    return (c[0] << 16) | (c[1] << 8) | c[2];
}
