#include "tui/tui.h"
#include "tui/stats_tui.h"
#include "utils/bitboard.h"
#include "utils/cli.h"
#include "utils/stats.h"
#include "game/records.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    /* Parse command-line flags ──────────────────────────────────────── */
    CliArgs args;
    if (cli_parse(argc, argv, &args) != 0) {
        fprintf(stderr, "dchess: %s\n", args.error_msg);
        fprintf(stderr, "Try: dchess --help\n");
        return 1;
    }

    if (args.show_help)    cli_help();     /* exits */
    if (args.show_version) cli_version();  /* exits */
    if (args.list_engines) cli_list_engines();   /* exits */
    if (args.list_profiles) cli_list_profiles(); /* exits */

    setlocale(LC_ALL, "");   /* required for ncurses unicode output */

    if (args.show_stats) {
        DchessStats s;
        ProfileList pl;
        char games[512];
        records_path(games, sizeof(games));
        if (profiles_load(&pl) && pl.count) {
            int i = args.profile[0] ? profiles_find(&pl, args.profile) : -1;
            profiles_stats(&pl.p[i >= 0 ? i : pl.active], games, &s);
        } else {
            stats_load(&s);
        }
        show_stats_overlay(&s);   /* full TUI stats window */
        return 0;
    }

    /* Normal game startup ───────────────────────────────────────────── */
    init_attacks();

    TUIState state;
    tui_init(&state, &args);
    tui_run(&state);

    return 0;
}
