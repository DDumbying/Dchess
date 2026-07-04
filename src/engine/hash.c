#include "engine/hash.h"

U64 hash_position(const Position *pos)
{
    U64 h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 12; i++) {
        h ^= pos->bitboards[i] * 0x9e3779b97f4a7c15ULL;
        h += (h << 6) + (h >> 2);
    }
    h ^= (U64)pos->side     * 0x517cc1b727220a95ULL;
    h ^= (U64)pos->castling * 0x6c62272e07bb0142ULL;
    h ^= (U64)(pos->enpassant + 1) * 0xbf58476d1ce4e5b9ULL;
    return h;
}
