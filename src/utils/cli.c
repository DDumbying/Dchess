#include "utils/cli.h"
#include "utils/stats.h"
#include "utils/constants.h"
#include "engine/board.h"
#include "engine/fen.h"
#include "utils/theme.h"
#include "utils/engines.h"
#include "game/records.h"
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Depth table ─────────────────────────────────────────────────────────── */
static const int diff_to_depth[3] = { 2, 5, 8 };

/* Time budget table (ms) ──────────────────────────────────────────────
 * Generous on purpose, but not unlimited: measured against the opening
 * position, this engine's depth 7 takes ~26s and depth 8 ~170s to
 * complete (no null-move pruning or late-move reductions to tame the
 * branching factor), while producing the *same* recommended move as
 * depth 6 did in well under a second. So a large budget mostly just
 * waits, without a corresponding strength gain, on calm positions --
 * hard's budget below is sized to usually finish depth 6 comfortably
 * and sometimes reach into depth 7 on sharper or simpler (lower
 * branching factor) positions, without committing to depth 7/8's full
 * cost on every single move. */
static const int diff_to_time_ms[3] = { 1500, 3000, 5000 };

int cli_depth_for_difficulty(int difficulty)
{
    if (difficulty < 0 || difficulty > 2) difficulty = DIFF_MEDIUM;
    return diff_to_depth[difficulty];
}

int cli_time_limit_for_difficulty(int difficulty)
{
    if (difficulty < 0 || difficulty > 2) difficulty = DIFF_MEDIUM;
    return diff_to_time_ms[difficulty];
}

/* Help page ───────────────────────────────────────────────────────────── */
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
        "            easy   – depth 2, up to 1.5s  (quick, forgiving)\n"
        "            medium – depth 5, up to 3s   (balanced)  [default]\n"
        "            hard   – depth 8, up to 5s   (challenging, slower)\n"
        "\n"
        "    -2, --two-player\n"
        "          Two people at one keyboard; no engine. The board flips\n"
        "          after each move so the next player faces their own pieces.\n"
        "\n"
        "    --white <human|guest|profile|easy|medium|hard|engine>\n"
        "    --black <human|guest|profile|easy|medium|hard|engine>\n"
        "          Choose who plays one side: a profile name, guest, a level\n"
        "          or an engine. Overrides -c, -d and -2.\n"
        "          Give both an engine level to watch it play itself:\n"
        "            dchess --white hard --black easy\n"
        "          An engine is a name from engines.conf, e.g.\n"
        "            dchess --white \"Stockfish 1500\" --black hard\n"
        "\n"
        "    --profile <name>\n"
        "          Play as this profile for this run.\n"
        "\n"
        "    --profiles\n"
        "          List the profiles with their records and exit.\n"
        "\n"
        "    --engines\n"
        "          List the registered UCI engines and exit.\n"
        "\n"
        "    --fen <string>\n"
        "          Start from a custom position instead of the standard setup.\n"
        "          Takes a full FEN string (quote it if your shell needs that).\n"
        "\n"
        "    -m, --menu\n"
        "          Show the interactive onboarding screen to pick the players/\n"
        "          starting position visually, even if other flags were given.\n"
        "\n"
        "    --no-menu\n"
        "          Skip the onboarding screen and start immediately, even with\n"
        "          no other flags given (restores the classic instant-start).\n"
        "\n"
        "    --theme <name>\n"
        "          Color theme: gruvbox | tokyonight | btop | catppuccin\n"
        "          (default: gruvbox). Also changeable from the onboarding\n"
        "          screen or the in-game 'theme <name>' command.\n"
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
        "    dchess                          Onboarding screen (pick options visually)\n"
        "    dchess --no-menu                Start immediately (white, medium)\n"
        "    dchess --color black            Play as black, no menu\n"
        "    dchess --difficulty hard        Play on hard difficulty, no menu\n"
        "    dchess -c black -d easy         Play as black on easy, no menu\n"
        "    dchess --fen \"<FEN string>\"      Start from a custom position\n"
        "    dchess --menu -d hard           Menu, pre-filled to hard difficulty\n"
        "    dchess --theme tokyonight       Start with the Tokyo Night theme\n"
        "    dchess --white hard --black hard\n"
        "                                    Watch the engine play itself\n"
        "    dchess --stats                  View your stats\n"
        "\n"
        "  IN-GAME COMMANDS  (type in the command bar at the bottom)\n"
        "    e2e4        Make a move in algebraic notation\n"
        "    go          Play one engine move for the side to move,\n"
        "                even while paused\n"
        "    stop        Have a thinking engine return its best move now,\n"
        "                instead of waiting out the rest of its time budget\n"
        "    undo / u    Take back your last move. Against the engine this\n"
        "                takes back its reply too, so the turn returns to you.\n"
        "                Between two engines it takes back one and pauses\n"
        "    new         Reset the board to a new game\n"
        "    flip        Turn the board around\n"
        "    pause / resume\n"
        "                Hold and restart the engines (Space does both)\n"
        "    white|black human\n"
        "    white|black engine [easy|medium|hard|name]\n"
        "                Change who plays a side, mid-game\n"
        "    swap        Exchange the two players\n"
        "    engines     List the registered UCI engines\n"
        "    depth N     Change search depth (1–8) mid-game\n"
        "    eval        Show the current position evaluation\n"
        "    fen         Show the current position as a FEN string\n"
        "    pgn [path]  Save the game as PGN. Defaults to\n"
        "                ~/.local/share/dchess/games/<date>-<time>.pgn\n"
        "    loadfen <FEN>\n"
        "                Load a custom position mid-game\n"
        "    theme <name>\n"
        "                Switch color theme: gruvbox | tokyonight | btop | catppuccin\n"
        "    help        List in-game commands\n"
        "    quit / q    Exit dchess\n"
        "\n"
        "  CURSOR CONTROLS\n"
        "    ← → ↑ ↓  or  h l k j    Move cursor\n"
        "    Enter                    Select piece / confirm move\n"
        "    Escape                   Deselect piece\n"
        "    u                        Take back the last move\n"
        "    Space                    Pause / resume the engines\n"
        "\n"
        "  STATS FILE\n"
        "    ~/.local/share/dchess/stats.dat\n"
        "\n",
        DCHESS_VERSION
    );
    exit(0);
}

/* Version ─────────────────────────────────────────────────────────────── */
void cli_version(void)
{
    printf("dchess %s\n", DCHESS_VERSION);
    exit(0);
}

/* Only a value that is not a built-in word reads the registry, so a
 * broken engines.conf never stops an ordinary start. */
static int known_engine(const char *name, char *err, size_t n)
{
    EngineList l;
    engines_load(&l);
    if (engines_find(&l, name)) return 1;
    if (!l.count) {
        snprintf(err, n, "Unknown player '%s'. Use: human | easy | medium | hard "
                         "(no engines registered)", name);
        return 0;
    }
    char names[160] = "";
    for (int i = 0; i < l.count; i++) {
        if (i) strncat(names, ", ", sizeof(names) - strlen(names) - 1);
        strncat(names, l.e[i].name, sizeof(names) - strlen(names) - 1);
    }
    snprintf(err, n, "Unknown player '%s'. Use: human | easy | medium | hard, "
                     "or an engine: %s", name, names);
    return 0;
}

static int known_profile(const char *name, char *err, size_t n)
{
    ProfileList l;
    profiles_load(&l);
    if (profiles_find(&l, name) >= 0) return 1;
    char names[160] = "";
    for (int i = 0; i < l.count; i++) {
        if (i) strncat(names, ", ", sizeof(names) - strlen(names) - 1);
        strncat(names, l.p[i].name, sizeof(names) - strlen(names) - 1);
    }
    snprintf(err, n, "Unknown profile '%s'. Profiles: %s", name, l.count ? names : "none yet");
    return 0;
}

void cli_list_profiles(void)
{
    ProfileList l;
    RecordList r;
    char games[512];
    profiles_load(&l);
    records_path(games, sizeof(games));
    records_load(games, &r);
    if (!l.count) {
        printf("No profiles yet. One is created the first time dchess starts.\n");
        exit(0);
    }
    printf("  %-26s %s\n", "PROFILE", "W-D-L");
    for (int i = 0; i < l.count; i++) {
        const Profile *p = &l.p[i];
        RecordTally t = records_tally(&r, p->name);
        printf("  %s %-24s %d-%d-%d\n", i == l.active ? "*" : " ", p->name,
               t.wins + p->legacy_wins, t.draws + p->legacy_draws, t.losses + p->legacy_losses);
    }
    records_free(&r);
    exit(0);
}

void cli_list_engines(void)
{
    EngineList l;
    char path[512];
    int have = engines_path(path, sizeof(path));
    engines_load(&l);
    if (!l.count) {
        printf("No engines registered%s%s.\n", have ? " in " : "", have ? path : "");
        printf("Add one from the start menu with the e key.\n");
        exit(0);
    }
    printf("  %-24s %-18s %s\n", "NAME", "STRENGTH", "PATH");
    for (int i = 0; i < l.count; i++) {
        char s[48];
        engine_strength_label(&l.e[i], s, sizeof(s));
        printf("  %-24s %-18s %s\n", l.e[i].name, s, l.e[i].path);
    }
    exit(0);
}

/* Parser ──────────────────────────────────────────────────────────────── */
int cli_parse(int argc, char **argv, CliArgs *args)
{
    /* defaults */
    args->players[WHITE] = player_human();
    args->players[BLACK] = player_builtin(DIFF_MEDIUM);

    /* -c, -d and -2 shape the game; --white and --black then override
     * their side whatever the order they came in. */
    int colour = WHITE, level = DIFF_MEDIUM, two = 0;
    int chosen_set[2] = { 0, 0 };
    Player chosen[2];
    args->show_version = 0;
    args->show_stats   = 0;
    args->list_engines = 0;
    args->list_profiles = 0;
    args->profile[0] = '\0';
    args->human_active[WHITE] = 1;
    args->human_active[BLACK] = 0;
    int chosen_active[2] = { 0, 0 };
    args->show_help    = 0;
    args->fen[0]        = '\0';
    args->menu          = 0;
    args->no_menu       = 0;
    args->theme         = 0;
    args->any_gameplay_flag = 0;
    args->error        = 0;
    args->error_msg[0] = '\0';

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        /* --help / -h ──────────────────────────────────────────────── */
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            args->show_help = 1;
            return 0;
        }

        /* --version / -V ───────────────────────────────────────────── */
        if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
            args->show_version = 1;
            return 0;
        }

        /* --stats / -s ─────────────────────────────────────────────── */
        if (strcmp(a, "--stats") == 0 || strcmp(a, "-s") == 0) {
            args->show_stats = 1;
            return 0;
        }

        /* --engines ─────────────────────────────────────────────────── */
        if (strcmp(a, "--profiles") == 0) {
            args->list_profiles = 1;
            return 0;
        }
        if (strcmp(a, "--profile") == 0) {
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '--profile' requires a profile name");
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            if (!known_profile(val, args->error_msg, sizeof(args->error_msg))) {
                args->error = 1;
                return -1;
            }
            snprintf(args->profile, sizeof(args->profile), "%s", val);
            continue;
        }
        if (strcmp(a, "--engines") == 0) {
            args->list_engines = 1;
            return 0;
        }

        /* --color / -c ─────────────────────────────────────────────── */
        if (strcmp(a, "--color") == 0 || strcmp(a, "-c") == 0) {
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '%s' requires an argument: white|black", a);
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            if (strcmp(val, "white") == 0 || strcmp(val, "w") == 0) {
                colour = WHITE;
            } else if (strcmp(val, "black") == 0 || strcmp(val, "b") == 0) {
                colour = BLACK;
            } else {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Unknown color '%s'. Use: white | black", val);
                args->error = 1;
                return -1;
            }
            args->any_gameplay_flag = 1;
            continue;
        }

        /* --difficulty / -d ────────────────────────────────────────── */
        if (strcmp(a, "--difficulty") == 0 || strcmp(a, "-d") == 0) {
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '%s' requires an argument: easy|medium|hard", a);
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            if (strcmp(val, "easy") == 0 || strcmp(val, "e") == 0) {
                level = DIFF_EASY;
            } else if (strcmp(val, "medium") == 0 || strcmp(val, "m") == 0) {
                level = DIFF_MEDIUM;
            } else if (strcmp(val, "hard") == 0 || strcmp(val, "h") == 0) {
                level = DIFF_HARD;
            } else {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Unknown difficulty '%s'. Use: easy | medium | hard", val);
                args->error = 1;
                return -1;
            }
            args->any_gameplay_flag = 1;
            continue;
        }

        /* --two-player / -2 ────────────────────────────────────────── */
        if (strcmp(a, "--two-player") == 0 || strcmp(a, "-2") == 0) {
            two = 1;
            args->any_gameplay_flag = 1;
            continue;
        }

        /* --white / --black ────────────────────────────────────────── */
        if (strcmp(a, "--white") == 0 || strcmp(a, "--black") == 0) {
            int side = (a[2] == 'w') ? WHITE : BLACK;
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '%s' requires an argument: human|easy|medium|hard", a);
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            int lv = players_level_from_name(val);
            char perr[256];
            chosen_active[side] = 0;
            if (strcmp(val, "human") == 0) {
                chosen[side] = player_human();
                chosen_active[side] = 1;
            } else if (strcasecmp(val, "guest") == 0) {
                chosen[side] = player_human();
            } else if (lv >= 0) {
                chosen[side] = player_builtin(lv);
            } else if (known_profile(val, perr, sizeof(perr))) {
                chosen[side] = player_profile(val);
            } else if (known_engine(val, args->error_msg, sizeof(args->error_msg))) {
                chosen[side] = player_uci(val);
            } else {
                args->error = 1;
                return -1;
            }
            chosen_set[side] = 1;
            args->any_gameplay_flag = 1;
            continue;
        }

        /* --fen <string> ───────────────────────────────────────────── */
        if (strcmp(a, "--fen") == 0) {
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '--fen' requires a FEN string argument");
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            Position probe;
            if (!parse_fen(val, &probe, NULL, NULL)) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Invalid FEN string: '%s'", val);
                args->error = 1;
                return -1;
            }
            snprintf(args->fen, sizeof(args->fen), "%s", val);
            args->any_gameplay_flag = 1;
            continue;
        }

        /* --menu / -m ───────────────────────────────────────────────── */
        if (strcmp(a, "--menu") == 0 || strcmp(a, "-m") == 0) {
            args->menu = 1;
            continue;
        }

        /* --no-menu ─────────────────────────────────────────────────── */
        if (strcmp(a, "--no-menu") == 0) {
            args->no_menu = 1;
            continue;
        }

        /* --theme <name> ───────────────────────────────────────────── */
        if (strcmp(a, "--theme") == 0) {
            if (i + 1 >= argc) {
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Option '--theme' requires a name argument");
                args->error = 1;
                return -1;
            }
            const char *val = argv[++i];
            int t = theme_from_name(val);
            if (t < 0) {
                char list[128] = {0};
                for (int ti = 0; ti < theme_count(); ti++) {
                    strncat(list, theme_name(ti), sizeof(list) - strlen(list) - 1);
                    if (ti + 1 < theme_count())
                        strncat(list, " | ", sizeof(list) - strlen(list) - 1);
                }
                snprintf(args->error_msg, sizeof(args->error_msg),
                         "Unknown theme '%s'. Use: %s", val, list);
                args->error = 1;
                return -1;
            }
            args->theme = t;
            args->any_gameplay_flag = 1;
            continue;
        }

        /* unknown flag ─────────────────────────────────────────────── */
        snprintf(args->error_msg, sizeof(args->error_msg),
                 "Unknown option '%s'. Try: dchess --help", a);
        args->error = 1;
        return -1;
    }

    /* The human side is the active profile; with -2, White is. */
    args->human_active[WHITE] = args->human_active[BLACK] = 0;
    if (two) {
        args->players[WHITE] = args->players[BLACK] = player_human();
        args->human_active[WHITE] = 1;
    } else {
        args->players[colour]         = player_human();
        args->players[colour ^ BLACK] = player_builtin(level);
        args->human_active[colour]    = 1;
    }
    for (int s = WHITE; s <= BLACK; s++)
        if (chosen_set[s]) {
            args->players[s] = chosen[s];
            args->human_active[s] = chosen_active[s];
        }

    return 0;
}
