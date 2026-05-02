#include "utils/cli.h"
#include "utils/stats.h"
#include "utils/constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Depth table ─────────────────────────────────────────────────────────── */
static const int diff_to_depth[3] = { 2, 5, 8 };

/* ── Help page ───────────────────────────────────────────────────────────── */
void cli_help(void)
{
    printf(
        "\n"
        "  dchess v%s  –  a terminal chess engine\n"
        "\n"
        "  USAGE\n"
        "    dchess [OPTIONS]\n"
        "\n"
        "  OPTIONS\n"
        "    -c, --color <white|black>\n"
        "          Choose which side you play as.\n"
        "          Default: white\n"
        "\n"
        "    -d, --difficulty <easy|medium|hard>\n"
        "          Set the engine strength.\n"
        "            easy   – depth 2  (quick, forgiving)\n"
        "            medium – depth 5  (balanced)  [default]\n"
        "            hard   – depth 8  (challenging, slower)\n"
        "\n"
        "    -s, --stats\n"
        "          Show your game statistics and exit.\n"
        "\n"
        "    -V, --version\n"
        "          Print version information and exit.\n"
        "\n"
        "    -h, --help\n"
        "          Show this help page and exit.\n"
        "\n"
        "  EXAMPLES\n"
        "    dchess                          Start with defaults (white, medium)\n"
        "    dchess --color black            Play as black\n"
        "    dchess --difficulty hard        Play on hard difficulty\n"
        "    dchess -c black -d easy         Play as black on easy\n"
        "    dchess --stats                  View your stats\n"
        "\n"
        "  IN-GAME COMMANDS  (type in the command bar at the bottom)\n"
        "    e2e4        Make a move in algebraic notation\n"
        "    go          Let the engine play the current side\n"
        "    new         Reset the board to a new game\n"
        "    flip        Swap which side the engine plays\n"
        "    depth N     Change search depth (1–8) mid-game\n"
        "    eval        Show the current position evaluation\n"
        "    help        List in-game commands\n"
        "    quit / q    Exit dchess\n"
        "\n"
        "  CURSOR CONTROLS\n"
        "    ← → ↑ ↓  or  h l k j    Move cursor\n"
        "    Enter                    Select piece / confirm move\n"
        "    Escape                   Deselect piece\n"
        "\n"
        "  STATS FILE\n"
        "    ~/.local/share/dchess/stats.dat\n"
        "\n",
        DCHESS_VERSION
    );
    exit(0);
}

/* ── Version ─────────────────────────────────────────────────────────────── */
void cli_version(void)
{
    printf("dchess %s\n", DCHESS_VERSION);
    exit(0);
}

/* ── Parser ──────────────────────────────────────────────────────────────── */
int cli_parse(int argc, char **argv, CliArgs *args)
{
    /* defaults */
    args->player_side  = WHITE;
    args->difficulty   = DIFF_MEDIUM;
    args->engine_depth = diff_to_depth[DIFF_MEDIUM];
    args->show_version = 0;
    args->show_stats   = 0;
    args->show_help    = 0;
    args->error        = 0;
    args->error_msg[0] = '\0';

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        /* ── --help / -h ──────────────────────────────────────────────── */
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            args->show_help = 1;
            return 0;
        }

        /* ── --version / -V ───────────────────────────────────────────── */
        if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
            args->show_version = 1;
            return 0;
        }

        /* ── --stats / -s ─────────────────────────────────────────────── */
        if (strcmp(a, "--stats") == 0 || strcmp(a, "-s") == 0) {
            args->show_stats = 1;
            return 0;
        }

        /* ── --color / -c ─────────────────────────────────────────────── */
        if (strcmp(a, "--color") == 0 || strcmp(a, "-c") == 0) {
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '%s' requires an argument: white|black", a);
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            if (strcmp(val, "white") == 0 || strcmp(val, "w") == 0) {
                args->player_side = WHITE;
            } else if (strcmp(val, "black") == 0 || strcmp(val, "b") == 0) {
                args->player_side = BLACK;
            } else {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Unknown color '%s'. Use: white | black", val);
                args->error = 1;
                return -1;
            }
            continue;
        }

        /* ── --difficulty / -d ────────────────────────────────────────── */
        if (strcmp(a, "--difficulty") == 0 || strcmp(a, "-d") == 0) {
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '%s' requires an argument: easy|medium|hard", a);
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            if (strcmp(val, "easy") == 0 || strcmp(val, "e") == 0) {
                args->difficulty   = DIFF_EASY;
            } else if (strcmp(val, "medium") == 0 || strcmp(val, "m") == 0) {
                args->difficulty   = DIFF_MEDIUM;
            } else if (strcmp(val, "hard") == 0 || strcmp(val, "h") == 0) {
                args->difficulty   = DIFF_HARD;
            } else {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Unknown difficulty '%s'. Use: easy | medium | hard", val);
                args->error = 1;
                return -1;
            }
            args->engine_depth = diff_to_depth[args->difficulty];
            continue;
        }

        /* ── unknown flag ─────────────────────────────────────────────── */
        snprintf(args->error_msg, sizeof(args->error_msg),
                 "Unknown option '%s'. Try: dchess --help", a);
        args->error = 1;
        return -1;
    }

    return 0;
}
