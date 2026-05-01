#ifndef CONSTANTS_H
#define CONSTANTS_H

#define BOARD_SIZE 64
#define MAX_MOVES  256
#define MAX_DEPTH  64
#define INF        1000000
#define MATE_SCORE 999000

enum { WHITE, BLACK, BOTH };

enum {
    P, N, B, R, Q, K,
    p, n, b, r, q, k
};

enum {
    a1,b1,c1,d1,e1,f1,g1,h1,
    a2,b2,c2,d2,e2,f2,g2,h2,
    a3,b3,c3,d3,e3,f3,g3,h3,
    a4,b4,c4,d4,e4,f4,g4,h4,
    a5,b5,c5,d5,e5,f5,g5,h5,
    a6,b6,c6,d6,e6,f6,g6,h6,
    a7,b7,c7,d7,e7,f7,g7,h7,
    a8,b8,c8,d8,e8,f8,g8,h8, NO_SQ
};

static inline const char* sq_name(int sq) {
    static const char* names[65] = {
        "a1","b1","c1","d1","e1","f1","g1","h1",
        "a2","b2","c2","d2","e2","f2","g2","h2",
        "a3","b3","c3","d3","e3","f3","g3","h3",
        "a4","b4","c4","d4","e4","f4","g4","h4",
        "a5","b5","c5","d5","e5","f5","g5","h5",
        "a6","b6","c6","d6","e6","f6","g6","h6",
        "a7","b7","c7","d7","e7","f7","g7","h7",
        "a8","b8","c8","d8","e8","f8","g8","h8","--"
    };
    if (sq < 0 || sq > 64) return "--";
    return names[sq];
}

#endif
