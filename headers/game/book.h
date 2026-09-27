#ifndef BOOK_H
#define BOOK_H

#include "game/game.h"
#include <stddef.h>

typedef struct Book Book;

/* Built from the compiled-in opening lines. */
Book *book_builtin(void);
/* A Polyglot .bin. NULL with a message in err on failure. */
Book *book_open(const char *path, char *err, size_t n);
void  book_free(Book *b);

/* The Polyglot key of a position. */
U64   book_key(const Position *pos);

/* A book move for the side to move, or 0 when out of book or past the
 * level's depth limit. rng is the caller's state, advanced by the call. */
Move  book_pick(const Book *b, const GameState *g, int level, unsigned *rng);

/* The deepest named opening the game has reached, or NULL. */
const char *book_opening(const GameState *g, const char **eco);

#endif
