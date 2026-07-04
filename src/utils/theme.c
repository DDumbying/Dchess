#include "utils/theme.h"
#include <strings.h>

static const Theme THEMES[] = {
    { "classic",
      {870,820,710}, {360,250,150}, {150,310,760}, {80,680,680}, {150,680,150},
      {80,120,500}, {820,130,100}, {990,880,200}, {780,830,540}, {390,510,180},
      {90,110,100}, {500,680,680} },
    { "midnight",
      {620,700,780}, {120,180,300}, {900,300,300}, {950,750,150}, {150,780,500},
      {200,500,900}, {950,150,150}, {990,880,200}, {500,650,780}, {150,300,480},
      {60,80,120}, {300,750,900} },
    { "forest",
      {780,760,520}, {180,320,120}, {500,280,150}, {950,780,200}, {850,500,150},
      {300,550,250}, {850,180,120}, {990,880,200}, {680,700,420}, {250,420,150},
      {70,100,60}, {550,720,350} },
    { "contrast",
      {1000,1000,1000}, {0,0,0}, {900,100,100}, {100,900,900}, {100,900,100},
      {900,900,100}, {1000,100,100}, {1000,850,0}, {850,850,300}, {300,300,50},
      {0,0,0}, {1000,1000,1000} },
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
