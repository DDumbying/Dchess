#ifndef PGN_H
#define PGN_H

#include "game/game.h"
#include <stddef.h>

/* Who played, for the tag pairs. Any field left NULL gets a sensible
 * default; the game itself supplies the result and the moves. */
typedef struct {
    const char *event;
    const char *site;
    const char *white;
    const char *black;
} PgnHeader;

/* Write `g` as PGN. Returns 0 on success, or a negative errno-style code
 * if the file could not be opened or written. */
int pgn_write(const GameState *g, const PgnHeader *h, const char *path);

/* Default destination: ~/.local/share/dchess/games/<date>-<time>.pgn,
 * creating the directory if needed. Returns 0 on success. */
int pgn_default_path(char *buf, size_t n);

#endif
