#ifndef PROFILES_H
#define PROFILES_H
#include "game/players.h"
#include "utils/engines.h"
#include "utils/stats.h"
#define PROFILES_MAX 32
#define PROFILE_NAME_MAX 24
typedef struct {
    char name[PLAYER_NAME_MAX + 1];          /* at most PROFILE_NAME_MAX characters */
    char theme[24], white[PLAYER_NAME_MAX + 1], black[PLAYER_NAME_MAX + 1];
    int  legacy_games, legacy_wins, legacy_losses, legacy_draws;
} Profile;
typedef struct { Profile p[PROFILES_MAX]; int count, active; } ProfileList;

int  profiles_path(char *buf, size_t n);
int  profiles_load(ProfileList *l);            /* 0 if the file does not exist */
int  profiles_save(const ProfileList *l);      /* via .tmp + rename */
int  profiles_check_name(const ProfileList *l, const EngineList *e, const char *name,
                         int skip, char *err, size_t n);
int  profiles_add(ProfileList *l, const EngineList *e, const char *name, char *err, size_t n);
/* Also relabels the profile's games in `games`. */
int  profiles_rename(ProfileList *l, const EngineList *e, int i, const char *name,
                     const char *games, char *err, size_t n);
int  profiles_remove(ProfileList *l, int i);   /* 0 for the last profile */
int  profiles_find(const ProfileList *l, const char *name);
/* Names with the active one first, for players_apply_command. */
int  profiles_names(const ProfileList *l, const char **out, int max);
/* No profiles.conf yet: create `user` (or "player"), import `old`, save. */
/* The profile's records plus its legacy totals, for the stats screens.
 * Legacy games and games against people or engines count under Medium. */
void profiles_stats(const Profile *p, const char *games, DchessStats *out);
int  profiles_first_run(ProfileList *l, const char *user, const DchessStats *old,
                        const char *games);
#endif
