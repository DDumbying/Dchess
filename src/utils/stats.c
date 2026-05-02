#include "utils/stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

/* ── File path helpers ───────────────────────────────────────────────────── */

#define STATS_MAGIC   0x44434853UL   /* "DCHS" */
#define STATS_VERSION 2

/* v1 struct (no history) kept only for migration */
typedef struct {
    int games_played[3];
    int wins[3];
    int losses[3];
    int draws[3];
    int total_moves;
    int total_time_secs;
    int longest_game_moves;
    int played_as_white;
    int played_as_black;
    int wins_as_white;
    int wins_as_black;
} DchessStatsV1;

typedef struct {
    unsigned long magic;
    int           version;
    DchessStats   data;
} StatsFile;

static void get_stats_path(char *buf, size_t n)
{
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(buf, n, "%s/.local/share/dchess/stats.dat", home);
}

static void ensure_dir(void)
{
    char path[512];
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(path, sizeof(path), "%s/.local", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.local/share", home);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/.local/share/dchess", home);
    mkdir(path, 0755);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int stats_load(DchessStats *s)
{
    memset(s, 0, sizeof(*s));
    char path[512];
    get_stats_path(path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    unsigned long magic   = 0;
    int           version = 0;
    fread(&magic,   sizeof(magic),   1, f);
    fread(&version, sizeof(version), 1, f);

    if (magic == STATS_MAGIC) {
        if (version == STATS_VERSION) {
            fread(&s->games_played,       sizeof(s->games_played),       1, f);
            fread(&s->wins,               sizeof(s->wins),               1, f);
            fread(&s->losses,             sizeof(s->losses),             1, f);
            fread(&s->draws,              sizeof(s->draws),              1, f);
            fread(&s->total_moves,        sizeof(s->total_moves),        1, f);
            fread(&s->total_time_secs,    sizeof(s->total_time_secs),    1, f);
            fread(&s->longest_game_moves, sizeof(s->longest_game_moves), 1, f);
            fread(&s->played_as_white,    sizeof(s->played_as_white),    1, f);
            fread(&s->played_as_black,    sizeof(s->played_as_black),    1, f);
            fread(&s->wins_as_white,      sizeof(s->wins_as_white),      1, f);
            fread(&s->wins_as_black,      sizeof(s->wins_as_black),      1, f);
            fread(&s->history_count,      sizeof(s->history_count),      1, f);
            if (s->history_count < 0 || s->history_count > DCHESS_MAX_HISTORY)
                s->history_count = 0;
            fread(s->history, sizeof(GameRecord), s->history_count, f);
        } else if (version == 1) {
            DchessStatsV1 old;
            memset(&old, 0, sizeof(old));
            fread(&old, sizeof(old), 1, f);
            memcpy(s->games_played, old.games_played, sizeof(old.games_played));
            memcpy(s->wins,         old.wins,         sizeof(old.wins));
            memcpy(s->losses,       old.losses,       sizeof(old.losses));
            memcpy(s->draws,        old.draws,        sizeof(old.draws));
            s->total_moves        = old.total_moves;
            s->total_time_secs    = old.total_time_secs;
            s->longest_game_moves = old.longest_game_moves;
            s->played_as_white    = old.played_as_white;
            s->played_as_black    = old.played_as_black;
            s->wins_as_white      = old.wins_as_white;
            s->wins_as_black      = old.wins_as_black;
            s->history_count      = 0;
        }
    }
    fclose(f);
    return 0;
}


int stats_save(const DchessStats *s)
{
    ensure_dir();
    char path[512];
    get_stats_path(path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    unsigned long magic   = STATS_MAGIC;
    int           version = STATS_VERSION;
    fwrite(&magic,   sizeof(magic),   1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&s->games_played,       sizeof(s->games_played),       1, f);
    fwrite(&s->wins,               sizeof(s->wins),               1, f);
    fwrite(&s->losses,             sizeof(s->losses),             1, f);
    fwrite(&s->draws,              sizeof(s->draws),              1, f);
    fwrite(&s->total_moves,        sizeof(s->total_moves),        1, f);
    fwrite(&s->total_time_secs,    sizeof(s->total_time_secs),    1, f);
    fwrite(&s->longest_game_moves, sizeof(s->longest_game_moves), 1, f);
    fwrite(&s->played_as_white,    sizeof(s->played_as_white),    1, f);
    fwrite(&s->played_as_black,    sizeof(s->played_as_black),    1, f);
    fwrite(&s->wins_as_white,      sizeof(s->wins_as_white),      1, f);
    fwrite(&s->wins_as_black,      sizeof(s->wins_as_black),      1, f);
    fwrite(&s->history_count,      sizeof(s->history_count),      1, f);
    int hc = (s->history_count > DCHESS_MAX_HISTORY) ? DCHESS_MAX_HISTORY : s->history_count;
    fwrite(s->history, sizeof(GameRecord), hc, f);
    fclose(f);
    return 0;
}

void stats_record(DchessStats *s,
                  int difficulty,
                  int result,
                  int player_side,
                  int moves,
                  int time_secs)
{
    if (difficulty < 0 || difficulty > 2) difficulty = 1;

    s->games_played[difficulty]++;
    if (result  ==  1) s->wins[difficulty]++;
    else if (result == -1) s->losses[difficulty]++;
    else               s->draws[difficulty]++;

    s->total_moves    += moves;
    s->total_time_secs += time_secs;
    if (moves > s->longest_game_moves)
        s->longest_game_moves = moves;

    if (player_side == 0) {   /* WHITE */
        s->played_as_white++;
        if (result == 1) s->wins_as_white++;
    } else {
        s->played_as_black++;
        if (result == 1) s->wins_as_black++;
    }

    /* Append to rolling history (drop oldest if full) */
    if (s->history_count >= DCHESS_MAX_HISTORY) {
        memmove(&s->history[0], &s->history[1],
                (DCHESS_MAX_HISTORY - 1) * sizeof(GameRecord));
        s->history_count = DCHESS_MAX_HISTORY - 1;
    }
    s->history[s->history_count].timestamp = (long)time(NULL);
    s->history[s->history_count].result    = result;
    s->history_count++;
}

/* ── Pretty printer ──────────────────────────────────────────────────────── */

static float winrate(int wins, int total)
{
    if (total == 0) return 0.0f;
    return 100.0f * wins / total;
}

static const char *diff_name[3] = { "Easy", "Medium", "Hard" };

void stats_print(const DchessStats *s)
{
    int total_games = s->games_played[0] + s->games_played[1] + s->games_played[2];
    int total_wins  = s->wins[0]  + s->wins[1]  + s->wins[2];
    int total_loss  = s->losses[0]+ s->losses[1]+ s->losses[2];
    int total_draw  = s->draws[0] + s->draws[1] + s->draws[2];

    printf("\n");
    printf("  ╔══════════════════════════════════════════╗\n");
    printf("  ║          dchess  –  Game Statistics      ║\n");
    printf("  ╠══════════════════════════════════════════╣\n");

    /* Overall */
    printf("  ║  %-14s  %4s  %4s  %4s  %6s  ║\n",
           "Difficulty", "Play", "Win", "Loss", "Win %%");
    printf("  ╠══════════════════════════════════════════╣\n");
    for (int d = 0; d < 3; d++) {
        printf("  ║  %-14s  %4d  %4d  %4d  %5.1f%%  ║\n",
               diff_name[d],
               s->games_played[d],
               s->wins[d],
               s->losses[d],
               winrate(s->wins[d], s->games_played[d]));
    }
    printf("  ╠══════════════════════════════════════════╣\n");
    printf("  ║  %-14s  %4d  %4d  %4d  %5.1f%%  ║\n",
           "Total",
           total_games, total_wins, total_loss,
           winrate(total_wins, total_games));

    /* Side stats */
    printf("  ╠══════════════════════════════════════════╣\n");
    printf("  ║  As White  – played %4d  won %4d  (%5.1f%%)  ║\n",
           s->played_as_white, s->wins_as_white,
           winrate(s->wins_as_white, s->played_as_white));
    printf("  ║  As Black  – played %4d  won %4d  (%5.1f%%)  ║\n",
           s->played_as_black, s->wins_as_black,
           winrate(s->wins_as_black, s->played_as_black));

    /* Misc */
    int avg_moves = total_games ? s->total_moves / total_games : 0;
    int avg_time  = total_games ? s->total_time_secs / total_games : 0;

    printf("  ╠══════════════════════════════════════════╣\n");
    printf("  ║  Draws              : %4d                ║\n", total_draw);
    printf("  ║  Avg moves/game     : %4d                ║\n", avg_moves);
    printf("  ║  Longest game       : %4d moves          ║\n", s->longest_game_moves);
    printf("  ║  Avg time/game      : %02d:%02d               ║\n",
           avg_time / 60, avg_time % 60);
    printf("  ║  Total play-time    : %02dh %02dm             ║\n",
           s->total_time_secs / 3600,
           (s->total_time_secs % 3600) / 60);
    printf("  ╚══════════════════════════════════════════╝\n");
    printf("\n");
}
