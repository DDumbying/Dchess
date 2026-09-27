#ifndef TEXT_H
#define TEXT_H

#include <stddef.h>

/* Terminal columns UTF-8 text takes; CJK counts two. */
int    text_width(const char *s);

/* Bytes of `s` that fit in `width` columns, never splitting a character. */
size_t text_fit(const char *s, int width);

#endif
