#ifndef TUI_COLORS_H
#define TUI_COLORS_H

/* Every init_pair() ID used anywhere in the TUI lives here and nowhere
 * else: pair numbers are global to the ncurses session.
 *
 *     1 - 55   CP_*
 *
 * Add a pair at the end. IDs must stay below 64, the pair count many
 * 8-colour terminals offer. */

#define CP_LIGHT        1   /* light square bg                    */
#define CP_DARK         2   /* dark  square bg                    */
#define CP_W_LIGHT      3   /* white piece fg on light sq         */
#define CP_W_DARK       4   /* white piece fg on dark  sq         */
#define CP_B_LIGHT      5   /* black piece fg on light sq         */
#define CP_B_DARK       6   /* black piece fg on dark  sq         */
#define CP_CURSOR       7   /* cursor highlight (no piece)        */
#define CP_CURSOR_PC    8   /* white piece on the cursor          */
#define CP_SEL          9   /* selected square bg                 */
#define CP_SEL_PC      10   /* white piece on the selection       */
#define CP_MOVE_HI     11   /* legal-move dest highlight bg       */
#define CP_MOVE_HI_PC  12   /* white piece on a legal destination */
#define CP_CHECK_SQ    13   /* king-in-check square               */
#define CP_CHECK_PC    14   /* white king on the check square     */
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
#define CP_SHADOW      36   /* panel drop shadow                  */
#define CP_FRAME       37   /* board frame / grid border          */

#define CP_ACC_BOARD   38
#define CP_ACC_EVAL    39
#define CP_ACC_CLOCK   40
#define CP_ACC_MOVES   41
#define CP_ACC_ENGINE  42
#define CP_TRACK       43
#define CP_RAMP_BASE   44   /* 44-51: gradient, low to high */

/* Black-piece counterparts of 8, 10, 12 and 14. The block-art pieces
 * share one silhouette, so colour is all that tells them apart. */
#define CP_CURSOR_PC_B  52
#define CP_SEL_PC_B     53
#define CP_MOVE_HI_PC_B 54
#define CP_CHECK_PC_B   55

#define CP_LAST         55

_Static_assert(CP_LAST < 64, "colour pair IDs must stay below 64");

#endif
