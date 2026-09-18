#ifndef TUI_COLORS_H
#define TUI_COLORS_H

/* ── ncurses color-pair and color-slot registry ──────────────────────────
 *
 * Every init_pair()/init_color() ID used anywhere in the TUI lives here,
 * and nowhere else. These are global to the ncurses session: two files
 * that pick the same number are talking about the same pair, whether or
 * not they meant to.
 *
 * This header exists because they used to be re-#defined per file, with
 * the numbers written out by hand -- render.c owned the real table while
 * tui.c and onboard.c each kept their own partial copy. Nothing checked
 * that the copies agreed, so renumbering a pair in render.c would
 * silently repaint whatever the stale copies pointed at.
 *
 * Ranges are carved up so they cannot collide:
 *     1 - 35   CP_*    main game UI  (render.c)
 *     8 - 20   COL_*   custom RGB slots, NOT pairs -- a different
 *                      namespace, so overlapping CP_* numbers is fine
 *    40 - 57   SCP_*   statistics screens (stats_tui.c)
 *    60 - 68   SCOL_*  custom RGB slots for the statistics screens
 *
 * Adding a pair means taking the next free number in the right range and
 * leaving a gap at the end of it, not renumbering what is already here.
 */

/* ── Game UI pairs (render.c) ───────────────────────────────────────────── */
#define CP_LIGHT        1   /* light square bg                    */
#define CP_DARK         2   /* dark  square bg                    */
#define CP_W_LIGHT      3   /* white piece fg on light sq         */
#define CP_W_DARK       4   /* white piece fg on dark  sq         */
#define CP_B_LIGHT      5   /* black piece fg on light sq         */
#define CP_B_DARK       6   /* black piece fg on dark  sq         */
#define CP_CURSOR       7   /* cursor highlight (no piece)        */
#define CP_CURSOR_PC    8   /* cursor highlight (piece)           */
#define CP_SEL          9   /* selected square bg                 */
#define CP_SEL_PC      10   /* selected square piece              */
#define CP_MOVE_HI     11   /* legal-move dest highlight bg       */
#define CP_MOVE_HI_PC  12   /* legal-move dest with piece         */
#define CP_CHECK_SQ    13   /* king-in-check square               */
#define CP_CHECK_PC    14   /* king piece on check sq             */
#define CP_LMVL        15   /* last-move light sq                 */
#define CP_LMVD        16   /* last-move dark  sq                 */
#define CP_W_LMVL      17   /* white piece on last-move light sq  */
#define CP_W_LMVD      18   /* white piece on last-move dark  sq  */
#define CP_B_LMVL      19   /* black piece on last-move light sq  */
#define CP_B_LMVD      20   /* black piece on last-move dark  sq  */
#define CP_BORDER      21
#define CP_TITLE       22
#define CP_LINK        23
#define CP_LABEL       24
#define CP_INFO_HEAD   25
#define CP_INFO_VAL    26
#define CP_STATUS_OK   27
#define CP_STATUS_ERR  28
#define CP_HINT        29
#define CP_CMD         30
#define CP_MOVE_W      31
#define CP_MOVE_B      32
#define CP_CAP_W       33
#define CP_CAP_B       34
#define CP_CANVAS      35

/* ── Custom RGB slots for the game UI (init_color, not init_pair) ───────── */
#define COL_LIGHT       8
#define COL_DARK        9
#define COL_WPFG       10   /* unused: white pieces use COLOR_YELLOW directly */
#define COL_BPFG       11
#define COL_CURSOR     12
#define COL_SEL        13
#define COL_MOVEHI     14
#define COL_CHECK      15
#define COL_GOLD       16
#define COL_LMVL       17
#define COL_LMVD       18
#define COL_CANVAS     19
#define COL_CHROME     20

/* ── Statistics screen pairs (stats_tui.c) ──────────────────────────────── */
#define SCP_BORDER     40
#define SCP_TITLE      41
#define SCP_HEAD       42
#define SCP_BAR_WIN    43
#define SCP_BAR_LOSS   44
#define SCP_BAR_DRAW   45
#define SCP_BAR_BG     46
#define SCP_VAL        47
#define SCP_HINT       48
#define SCP_LABEL      49
#define SCP_GOOD       50
#define SCP_BAD        51
#define SCP_NEUT       52
#define SCP_GRAPH_AX   53
#define SCP_GRAPH_W    54
#define SCP_GRAPH_L    55
#define SCP_GRAPH_D    56
#define SCP_GRAPH_BG   57

/* ── Custom RGB slots for the statistics screens ────────────────────────── */
#define SCOL_TEAL      60
#define SCOL_GOLD      61
#define SCOL_RUST      62
#define SCOL_SLATE     63
#define SCOL_MIST      64
#define SCOL_BARK      65
#define SCOL_LIME      66
#define SCOL_CORAL     67
#define SCOL_SKY       68

#endif
