#ifndef CLI_H
#define CLI_H

#include "game/players.h"

/* Difficulty maps to engine search depth. */
#define DIFF_EASY   0    /* depth 2  */
#define DIFF_MEDIUM 1    /* depth 5  */
#define DIFF_HARD   2    /* depth 8  */

typedef struct {
    Player players[2];  /* by colour; from -c/-d/-2, then --white/--black */
    int show_version;   /* --version flag */
    int show_stats;     /* --stats flag    */
    int list_engines;   /* --engines flag */
    int show_help;      /* --help flag     */
    char fen[128];       /* --fen <string>: custom starting position, empty = standard start */
    int menu;            /* --menu flag: force the interactive onboarding screen */
    int no_menu;         /* --no-menu flag: force-skip onboarding even with no other flags */
    int theme;           /* --theme <name>: color theme index, default 0 ("classic") */
    int any_gameplay_flag; /* any of -c -d -2 --white --black --fen --theme */
    int error;          /* set on bad argument */
    char error_msg[256];
} CliArgs;

/* Returns 0 on success. */
int  cli_parse(int argc, char **argv, CliArgs *args);

/* Shared with the onboarding screen so the mapping is not duplicated. */
int  cli_depth_for_difficulty(int difficulty);

/* Milliseconds paired with each difficulty's depth cap; whichever runs
 * out first ends the search. */
int  cli_time_limit_for_difficulty(int difficulty);

void cli_help(void);
void cli_version(void);

/* Prints the registered UCI engines and exits. */
void cli_list_engines(void);

#endif /* CLI_H */
