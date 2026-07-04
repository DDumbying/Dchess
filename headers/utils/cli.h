#ifndef CLI_H
#define CLI_H

/* ─────────────────────────────────────────────────────────────
 * dchess  –  command-line argument handling
 * ───────────────────────────────────────────────────────────── */

/* Difficulty levels map to engine search depth */
#define DIFF_EASY   0    /* depth 2  */
#define DIFF_MEDIUM 1    /* depth 5  */
#define DIFF_HARD   2    /* depth 8  */

typedef struct {
    int player_side;    /* WHITE(0) or BLACK(1), default WHITE */
    int difficulty;     /* DIFF_EASY / DIFF_MEDIUM / DIFF_HARD */
    int engine_depth;   /* derived from difficulty */
    int show_version;   /* --version flag */
    int show_stats;     /* --stats flag    */
    int show_help;      /* --help flag     */
    int two_player;     /* --two-player flag: no engine, board flips */
    char fen[128];       /* --fen <string>: custom starting position, empty = standard start */
    int menu;            /* --menu flag: force the interactive onboarding screen */
    int no_menu;         /* --no-menu flag: force-skip onboarding even with no other flags */
    int any_gameplay_flag; /* set if --color/--difficulty/--two-player/--fen was given */
    int error;          /* set on bad argument */
    char error_msg[256];
} CliArgs;

/* Parse argc/argv into *args.  Returns 0 on success. */
int  cli_parse(int argc, char **argv, CliArgs *args);

/* Same table cli_parse() uses for -d/--difficulty, exposed so the TUI
 * onboarding screen can derive engine_depth from a difficulty choice
 * without duplicating the mapping. */
int  cli_depth_for_difficulty(int difficulty);

/* Print the help page and exit(0). */
void cli_help(void);

/* Print version string and exit(0). */
void cli_version(void);

#endif /* CLI_H */
