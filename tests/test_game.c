/* Game-state unit tests.
 *
 * The rules bookkeeping -- the move log, the halfmove clock, the
 * repetition history and the game-over verdict -- used to live inside
 * TUIState and be driven from two different files, which made it
 * impossible to test without standing up ncurses. Now that it is a plain
 * struct behind game/game.h, it can be exercised directly.
 *
 * The repetition test in particular is a regression guard: the original
 * check counted the current position plus one earlier occurrence and
 * declared a draw, so games were called drawn on the SECOND repetition
 * rather than the third.
 *
 * Build & run:
 *   make test
 *   /tmp/test_game
 */
#include <stdio.h>
#include <string.h>
#include "game/game.h"
#include "engine/movegen.h"
#include "engine/make.h"
#include "engine/search.h"
#include "engine/move.h"
#include "utils/bitboard.h"
#include "utils/constants.h"
#include "test_common.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("  %-58s %s\n", name, ok ? "OK" : "FAIL");
    if (!ok) failures++;
}

/* Play a move given in coordinate notation, asserting it was legal. */
static int play(GameState *g, const char *text)
{
    int from, to, promo;
    Move m;
    if (!parse_move_str(text, &from, &to, &promo)) return 0;
    if (!game_find_move(g, from, to, promo, &m))   return 0;
    game_play(g, m);
    return 1;
}

/* ── Reset ──────────────────────────────────────────────────────────────── */

static void test_reset(void)
{
    printf("== game_reset ==\n");

    GameState g;
    memset(&g, 0xAB, sizeof(g));   /* poison: reset must clear everything */
    game_reset(&g);

    check("starts with no moves played", g.move_count == 0);
    check("halfmove clock starts at 0", g.halfmove_clock == 0);
    check("game is not over", g.game_over == 0 && g.result[0] == '\0');
    check("eval history is empty", g.eval_count == 0);
    check("clocks start at zero",
          g.white_clock == 0 && g.black_clock == 0 && g.clock_started == 0);
    check("White is to move", g.pos.side == WHITE);
    check("starting position is recorded for repetition",
          g.position_count == 1);
    check("White has 20 opening moves", has_legal_moves(&g.pos) == 1);
}

/* ── Move logging ───────────────────────────────────────────────────────── */

static void test_move_log(void)
{
    printf("== move log ==\n");

    GameState g;
    game_reset(&g);

    check("e2e4 is legal", play(&g, "e2e4") == 1);
    check("move_count advanced", g.move_count == 1);
    check("move logged in SAN", strcmp(g.move_history[0], "e4") == 0);
    check("the move itself is kept for the last-move highlight",
          FROM(g.move_made[0]) == e2 && TO(g.move_made[0]) == e4);
    check("moving piece recorded as a white pawn", g.move_piece[0] == P);
    check("position recorded for repetition", g.position_count == 2);
    check("side to move flipped to Black", g.pos.side == BLACK);

    check("e7e5 is legal", play(&g, "e7e5") == 1);
    check("second move logged", g.move_count == 2 &&
                                strcmp(g.move_history[1], "e5") == 0);
    check("black pawn recorded", g.move_piece[1] == p);
}

/* ── Halfmove clock (the 50-move rule counter) ──────────────────────────── */

static void test_halfmove_clock(void)
{
    printf("== halfmove clock ==\n");

    GameState g;
    game_reset(&g);

    play(&g, "g1f3");
    check("a knight move increments the clock", g.halfmove_clock == 1);
    play(&g, "g8f6");
    check("and again for Black", g.halfmove_clock == 2);

    play(&g, "e2e4");
    check("a pawn move resets the clock", g.halfmove_clock == 0);

    /* Set up a capture: 1...d5 2.exd5 */
    play(&g, "d7d5");
    check("clock reset by Black's pawn move too", g.halfmove_clock == 0);
    play(&g, "e4d5");
    check("a capture resets the clock", g.halfmove_clock == 0);

    play(&g, "f6d5");
    check("recapture also resets", g.halfmove_clock == 0);
    play(&g, "f3e5");
    check("quiet move increments again", g.halfmove_clock == 1);
}

/* ── Threefold repetition ───────────────────────────────────────────────── */

static void test_threefold_repetition(void)
{
    printf("== threefold repetition ==\n");

    GameState g;
    game_reset(&g);

    /* Shuffle both knights out and back. Each full cycle of four moves
     * returns to the starting position. */
    const char *cycle[] = { "g1f3", "g8f6", "f3g1", "f6g8" };

    for (int i = 0; i < 4; i++) play(&g, cycle[i]);
    game_update_status(&g);
    check("startpos seen twice is NOT a draw", g.game_over == 0);

    for (int i = 0; i < 4; i++) play(&g, cycle[i]);
    game_update_status(&g);
    check("startpos seen three times IS a draw", g.game_over == 1);
    check("result names repetition",
          strstr(g.result, "repetition") != NULL);
}

/* ── Checkmate / stalemate ──────────────────────────────────────────────── */

static void test_game_over(void)
{
    printf("== game over detection ==\n");

    /* Fool's mate: 1.f3 e5 2.g4 Qh4# */
    GameState g;
    game_reset(&g);
    play(&g, "f2f3");
    play(&g, "e7e5");
    play(&g, "g2g4");
    play(&g, "d8h4");
    game_update_status(&g);
    check("fool's mate is detected", g.game_over == 1);
    check("result says checkmate and names Black",
          strstr(g.result, "Checkmate") && strstr(g.result, "Black"));

    /* Classic stalemate: black king on h8, white king f7, white queen g6.
     * Black is to move, not in check, and has no legal move. */
    GameState st;
    game_reset(&st);
    setup_position(&st.pos,
        "........" "........" "........" "........"
        "........" "......Q." ".....K.." ".......k",
        BLACK, 0, -1);
    st.game_over = 0;
    st.result[0] = '\0';
    st.position_count = 0;
    st.halfmove_clock = 0;
    game_update_status(&st);
    check("stalemate is detected", st.game_over == 1);
    check("result says stalemate and draw",
          strstr(st.result, "Stalemate") && strstr(st.result, "Draw"));
}

/* ── Insufficient material ──────────────────────────────────────────────── */

/* Set up a bare position and ask for a verdict, with no move history in
 * play so only the material rule can fire. */
static void verdict(GameState *g, const char board[64], int side)
{
    game_reset(g);
    setup_position(&g->pos, board, side, 0, -1);
    g->game_over = 0;
    g->result[0] = '\0';
    g->halfmove_clock = 0;
    g->position_count = 0;
    game_update_status(g);
}

static void test_insufficient_material(void)
{
    printf("== insufficient material ==\n");

    GameState g;

    verdict(&g, "K......." "........" "........" "........"
                "........" "........" "........" ".......k", WHITE);
    check("K vs K is a draw", g.game_over == 1);
    check("and says so", strstr(g.result, "Insufficient material") != NULL);

    verdict(&g, "K....B.." "........" "........" "........"
                "........" "........" "........" ".......k", WHITE);
    check("K+B vs K is a draw", g.game_over == 1);

    verdict(&g, "K....N.." "........" "........" "........"
                "........" "........" "........" ".......k", WHITE);
    check("K+N vs K is a draw", g.game_over == 1);

    /* Both bishops on dark squares (a1 and h8 share a colour). */
    verdict(&g, "KB......" "........" "........" "........"
                "........" "........" "........" "......bk", WHITE);
    check("K+B vs K+B on the same colour is a draw", g.game_over == 1);

    /* b1 is dark, b8 is light -- mate remains possible. */
    verdict(&g, "KB......" "........" "........" "........"
                "........" "........" "........" ".b.....k", WHITE);
    check("K+B vs K+B on opposite colours is NOT a draw", g.game_over == 0);

    verdict(&g, "K....R.." "........" "........" "........"
                "........" "........" "........" ".......k", WHITE);
    check("K+R vs K is NOT a draw", g.game_over == 0);

    verdict(&g, "K......." ".....P.." "........" "........"
                "........" "........" "........" ".......k", WHITE);
    check("K+P vs K is NOT a draw", g.game_over == 0);

    verdict(&g, "K...BN.." "........" "........" "........"
                "........" "........" "........" ".......k", WHITE);
    check("K+B+N vs K is NOT a draw (forced mate exists)", g.game_over == 0);

    verdict(&g, "K...NN.." "........" "........" "........"
                "........" "........" "........" ".......k", WHITE);
    check("K+N+N vs K is NOT a draw (mate is possible)", g.game_over == 0);

    /* A checkmate must still be reported as a checkmate, not as a draw,
     * even when the surviving material could not mate again. */
    verdict(&g, "........" "........" "........" "........"
                "........" ".....K.." "......Q." ".......k", BLACK);
    check("checkmate still outranks the material rule",
          g.game_over == 1 && strstr(g.result, "Checkmate") != NULL);
}

/* ── Long games ─────────────────────────────────────────────────────────── */

/* Play the first legal move that does NOT end the game, so a game can be
 * driven for an arbitrary number of plies. Returns 0 when every legal
 * move would end it (or there are none). */
static int step_keeping_alive(GameState *g)
{
    MoveList ml;
    generate_moves(&g->pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Position legal = g->pos;
        if (!make_move(&legal, ml.moves[i])) continue;

        GameState trial = *g;
        game_play(&trial, ml.moves[i]);
        game_update_status(&trial);
        if (!trial.game_over) { *g = trial; return 1; }
    }
    return 0;
}

/* Play the first legal move, whatever it leads to. */
static int step_any(GameState *g)
{
    MoveList ml;
    generate_moves(&g->pos, &ml);

    for (int i = 0; i < ml.count; i++) {
        Position legal = g->pos;
        if (!make_move(&legal, ml.moves[i])) continue;
        game_play(g, ml.moves[i]);
        game_update_status(g);
        return 1;
    }
    return 0;
}

static void test_long_game(void)
{
    printf("== long games ==\n");

    /* Draw detection must survive a game longer than the repetition ring,
     * which is the whole point of the ring: the old flat array simply
     * stopped recording once full and silently gave up on threefold for
     * the rest of the game. Here the ring wraps and must keep working. */
    GameState g;
    game_reset(&g);

    int plies = 0;
    while (plies < 150 && step_keeping_alive(&g)) plies++;

    check("a game can run past the repetition window",
          g.position_count > GAME_REPETITION_WINDOW);
    check("and is not wrongly declared over", g.game_over == 0);

    /* The ring has now wrapped. Threefold must still be detected. */
    int extra = 0;
    while (extra < 200 && !g.game_over && step_any(&g)) extra++;
    check("threefold is still detected after the ring wraps",
          g.game_over == 1 && strstr(g.result, "repetition") != NULL);

    /* The 50-move rule, driven all the way to 100 half-moves. */
    GameState f;
    game_reset(&f);
    int n = 0;
    while (n < 400 && step_keeping_alive(&f)) n++;
    check("a shuffling game reaches 99 half-moves without a draw",
          f.halfmove_clock == 99 && f.game_over == 0);
    step_any(&f);
    check("the 100th half-move is a 50-move draw",
          f.game_over == 1 && strstr(f.result, "50-move") != NULL);
}

/* ── Undo ───────────────────────────────────────────────────────────────── */

/* Put a specific position on the board with a clean history. */
static void load_board(GameState *g, const char board[64], int side,
                       int castling, int enpassant)
{
    game_reset(g);
    setup_position(&g->pos, board, side, castling, enpassant);
    g->game_over = 0;
    g->result[0] = '\0';
    g->halfmove_clock = 0;
    g->position_count = 0;
    g->move_count = 0;
}

static void test_undo_basics(void)
{
    printf("== undo: basics ==\n");

    GameState g;
    game_reset(&g);

    check("nothing to undo in the starting position", game_can_undo(&g) == 0);
    check("and game_undo() refuses", game_undo(&g) == 0);

    Position before = g.pos;
    int positions_before = g.position_count;

    play(&g, "e2e4");
    check("a move can be taken back", game_can_undo(&g) == 1);
    check("game_undo() reports success", game_undo(&g) == 1);

    check("the position is restored exactly",
          memcmp(&before, &g.pos, sizeof(Position)) == 0);
    check("the move log shrinks", g.move_count == 0);
    check("repetition bookkeeping rewinds",
          g.position_count == positions_before);
    check("and there is nothing left to undo", game_can_undo(&g) == 0);
}

static void test_undo_restores_state(void)
{
    printf("== undo: bookkeeping ==\n");

    GameState g;
    game_reset(&g);

    play(&g, "g1f3");                  /* quiet move: clock goes to 1 */
    play(&g, "g8f6");                  /* and to 2                    */
    check("halfmove clock is 2 before undo", g.halfmove_clock == 2);

    play(&g, "e2e4");                  /* pawn move: clock resets     */
    check("pawn move reset the clock", g.halfmove_clock == 0);

    game_undo(&g);
    check("undo restores the previous halfmove clock", g.halfmove_clock == 2);
    check("and the move count", g.move_count == 2);

    game_record_eval(&g, 42);
    int evals = g.eval_count;
    play(&g, "d2d4");
    game_record_eval(&g, 99);
    game_undo(&g);
    check("undo drops the evaluation recorded for that ply",
          g.eval_count == evals);

    /* A finished game comes back to life when the mating move is taken
     * back -- otherwise undo would leave the board playable but the
     * game still flagged over. */
    GameState m;
    game_reset(&m);
    play(&m, "f2f3"); play(&m, "e7e5"); play(&m, "g2g4"); play(&m, "d8h4");
    game_update_status(&m);
    check("fool's mate ends the game", m.game_over == 1);
    game_undo(&m);
    check("undoing the mate resumes the game",
          m.game_over == 0 && m.result[0] == '\0');
    check("and legal moves are available again", has_legal_moves(&m.pos) == 1);
}

static void test_undo_special_moves(void)
{
    printf("== undo: special moves ==\n");

    GameState g;
    Position before;

    /* Capture: the taken piece has to come back. */
    load_board(&g, "K......." "........" "........" "....P..."
                   "...p...." "........" "........" ".......k",
               WHITE, 0, -1);
    before = g.pos;
    check("exd5 is legal", play(&g, "e4d5") == 1);
    game_undo(&g);
    check("undoing a capture restores the captured piece",
          memcmp(&before, &g.pos, sizeof(Position)) == 0);

    /* En passant: both the capturing and the captured pawn move. */
    load_board(&g, "K......." "........" "........" "........"
                   "...pP..." "........" "........" ".......k",
               /* Black has just played d7-d5, so the square it can be
                * captured on is d6, not the square it sits on. */
               WHITE, 0, d6);
    before = g.pos;
    check("e5xd6 e.p. is legal", play(&g, "e5d6") == 1);
    game_undo(&g);
    check("undoing en passant restores both pawns",
          memcmp(&before, &g.pos, sizeof(Position)) == 0);

    /* Castling moves two pieces and forfeits rights. */
    load_board(&g, "R...K..R" "........" "........" "........"
                   "........" "........" "........" "....k...",
               WHITE, CASTLE_WHITE_KING | CASTLE_WHITE_QUEEN, -1);
    before = g.pos;
    check("O-O is legal", play(&g, "e1g1") == 1);
    game_undo(&g);
    check("undoing a castle restores king, rook and rights",
          memcmp(&before, &g.pos, sizeof(Position)) == 0);

    /* Promotion replaces the pawn with a new piece. */
    load_board(&g, "K......." "........" "........" "........"
                   "........" "........" ".P......" ".......k",
               WHITE, 0, -1);
    before = g.pos;
    check("b7b8 promotion is legal", play(&g, "b7b8") == 1);
    game_undo(&g);
    check("undoing a promotion restores the pawn",
          memcmp(&before, &g.pos, sizeof(Position)) == 0);
}

static void test_undo_repeated(void)
{
    printf("== undo: unwinding a whole game ==\n");

    GameState g;
    game_reset(&g);
    Position start = g.pos;

    const char *moves[] = { "e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6" };
    for (int i = 0; i < 6; i++) play(&g, moves[i]);
    check("six plies played", g.move_count == 6);

    int undone = 0;
    while (game_can_undo(&g)) { game_undo(&g); undone++; }

    check("every ply can be taken back", undone == 6);
    check("the board is back to the starting position",
          memcmp(&start, &g.pos, sizeof(Position)) == 0);
    check("the move log is empty", g.move_count == 0);
    check("the clocks are back to zero",
          g.white_clock == 0 && g.black_clock == 0);
    check("repetition history holds only the start",
          g.position_count == 1);
}

/* ── Move lookup ────────────────────────────────────────────────────────── */

static void test_find_move(void)
{
    printf("== game_find_move ==\n");

    GameState g;
    game_reset(&g);

    Move m;
    check("finds a legal opening move",
          game_find_move(&g, e2, e4, 0, &m) == 1);
    check("rejects a move no piece can make",
          game_find_move(&g, e2, e5, 0, &m) == 0);
    check("rejects moving an empty square",
          game_find_move(&g, e4, e5, 0, &m) == 0);

    /* A pinned piece: white king e1, white knight e2, black rook e8.
     * The knight cannot move without exposing the king. */
    GameState pin;
    game_reset(&pin);
    setup_position(&pin.pos,
        "....K..." "....N..." "........" "........"
        "........" "........" "........" "....r...",
        WHITE, 0, -1);
    check("rejects a move that leaves the king in check",
          game_find_move(&pin, e2, d4, 0, &m) == 0);

    /* Promotion defaults to a queen when none is requested. */
    GameState pr;
    game_reset(&pr);
    setup_position(&pr.pos,
        "K......." "........" "........" "........"
        "........" "........" ".P......" ".......k",
        WHITE, 0, -1);
    check("b7b8 promotion is found", game_find_move(&pr, b7, b8, 0, &m) == 1);
    check("and defaults to a queen", (FLAGS(m) & FLAG_PROMO_Q) != 0);
}

/* ── FEN loading ────────────────────────────────────────────────────────── */

static void test_load_fen(void)
{
    printf("== game_load_fen ==\n");

    GameState g;
    game_reset(&g);
    play(&g, "e2e4");

    GameState before = g;
    check("a malformed FEN is rejected",
          game_load_fen(&g, "not a fen at all") == 0);
    check("and leaves the game completely untouched",
          memcmp(&before, &g, sizeof(GameState)) == 0);

    check("a valid FEN loads",
          game_load_fen(&g, "8/8/8/8/8/8/8/K6k w - - 5 12") == 1);
    check("halfmove clock comes from the FEN", g.halfmove_clock == 5);
    check("the loaded position is recorded for repetition",
          g.position_count == 1);
    check("the move log is cleared of the old game", g.move_count != 1);
    check("loading clears a previous game-over verdict",
          g.game_over == 0 && g.result[0] == '\0');
}

/* ── Piece lookup ───────────────────────────────────────────────────────── */

static void test_piece_at(void)
{
    printf("== game_piece_at ==\n");

    GameState g;
    game_reset(&g);

    check("finds the white king on e1", game_piece_at(&g, e1) == K);
    check("finds a black pawn on e7", game_piece_at(&g, e7) == p);
    check("reports -1 for an empty square", game_piece_at(&g, e4) == -1);
    check("reports -1 for an off-board index", game_piece_at(&g, -1) == -1);
    check("reports -1 past the last square", game_piece_at(&g, 64) == -1);
}

static void test_eval_perspective(void)
{
    printf("== evaluation perspective ==\n");

    check("a White-to-move score is already White's view",
          eval_white_view(925, WHITE) == 925);
    check("a Black-to-move score is negated",
          eval_white_view(-895, BLACK) == 895);

    /* White is a queen up; whoever is to move, White's view is positive. */
    GameState g;
    game_reset(&g);
    game_load_fen(&g, "4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
    SearchResult w = search(&g.pos, 3, 0);
    check("queen up, White to move: positive for White",
          eval_white_view(w.best_score, g.pos.side) > 500);

    game_load_fen(&g, "4k3/8/8/8/8/8/8/3QK3 b - - 0 1");
    SearchResult b = search(&g.pos, 3, 0);
    check("queen up, Black to move: still positive for White",
          eval_white_view(b.best_score, g.pos.side) > 500);

    /* A 200 ms budget that cannot complete depth 64 must report ~200 ms. */
    GameState t;
    game_reset(&t);
    SearchResult timed = search(&t.pos, 64, 200);
    check("a search reports how long it took",
          timed.elapsed_ms >= 150 && timed.elapsed_ms < 2000);
}

int main(void)
{
    init_attacks();

    test_reset();
    test_move_log();
    test_halfmove_clock();
    test_threefold_repetition();
    test_game_over();
    test_insufficient_material();
    test_long_game();
    test_undo_basics();
    test_undo_restores_state();
    test_undo_special_moves();
    test_undo_repeated();
    test_eval_perspective();
    test_find_move();
    test_load_fen();
    test_piece_at();

    if (failures) {
        printf("\n%d game test(s) FAILED.\n", failures);
        return 1;
    }
    printf("\nAll game tests passed.\n");
    return 0;
}
