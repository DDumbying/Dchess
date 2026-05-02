#include "tui/tui.h"
#include "tui/stats_tui.h"
#include "utils/bitboard.h"
#include "utils/cli.h"
#include "utils/stats.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    /* ── Parse command-line flags ──────────────────────────────────────── */
    CliArgs args;
    if (cli_parse(argc, argv, &args) != 0) {
        fprintf(stderr, "dchess: %s\n", args.error_msg);
        fprintf(stderr, "Try: dchess --help\n");
        return 1;
    }

    if (args.show_help)    cli_help();     /* exits */
    if (args.show_version) cli_version();  /* exits */

    setlocale(LC_ALL, "");   /* required for ncurses unicode output */

    if (args.show_stats) {
        DchessStats s;
        stats_load(&s);
        show_stats_overlay(&s);   /* full TUI stats window */
        return 0;
    }

    /* ── Normal game startup ───────────────────────────────────────────── */
    init_attacks();

    TUIState state;
    tui_init(&state, &args);
    tui_run(&state);

    return 0;
}
