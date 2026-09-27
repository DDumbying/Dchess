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

    setlocale(LC_ALL, "");   /* unicode output, and text widths */

    if (args.show_help)    cli_help();     /* exits */
    if (args.show_version) cli_version();  /* exits */
    if (args.list_engines) cli_list_engines();   /* exits */
    if (args.list_profiles) cli_list_profiles(); /* exits */


    if (args.show_stats) args.no_menu = 1;   /* the stats page instead */

    /* Normal game startup ───────────────────────────────────────────── */
    static ReplayList games;
    if (args.replay[0]) {
        char err[256];
        if (!replay_list(args.replay, &games, err, sizeof(err))) {
            fprintf(stderr, "dchess: %s\n", err);
            return 1;
        }
        if (!games.count) {
            fprintf(stderr, "dchess: no games in %s\n", args.replay);
            return 1;
        }
    }

    init_attacks();

    TUIState state;
    tui_init(&state, &args);
    state.stats_only = args.show_stats;
    if (args.replay[0]) {
        state.replay_list = &games;
        snprintf(state.replay_path, sizeof(state.replay_path), "%s", args.replay);
    }
    tui_run(&state);
    replay_list_free(&games);

    return 0;
}
