#ifndef OPENINGS_H
#define OPENINGS_H

/* A named main line, in long algebraic moves from the start position. */
typedef struct {
    const char *eco;
    const char *name;
    const char *moves;
} OpeningLine;

extern const OpeningLine OPENINGS[];
extern const int OPENINGS_COUNT;

#endif
