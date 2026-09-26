#ifndef ENGINES_H
#define ENGINES_H

#include <stddef.h>

#define ENGINES_MAX     32
#define ENGINE_NAME_MAX 40

/* One named setup from engines.conf. */
typedef struct {
    char name[ENGINE_NAME_MAX + 1];
    char path[256];
    int  limit_depth;   /* 0 = limited by time instead */
    int  limit_ms;
    int  elo;           /* 0 = full strength */
} EngineEntry;

typedef struct {
    EngineEntry e[ENGINES_MAX];
    int         count;
} EngineList;

/* $XDG_CONFIG_HOME/dchess/engines.conf, else ~/.config/dchess/engines.conf.
 * 0 when neither variable is set. */
int  engines_path(char *buf, size_t n);

/* 1 if the file was read. A missing file is an empty list; bad lines are
 * skipped. */
int  engines_load(EngineList *l);
int  engines_save(const EngineList *l);

/* Whether `name` may be used by any entry other than number `skip`
 * (-1 for none). `err` may be NULL. */
int  engines_check_name(const EngineList *l, const char *name, int skip,
                        char *err, size_t n);
int  engines_add(EngineList *l, const EngineEntry *e, char *err, size_t n);
int  engines_replace(EngineList *l, int index, const EngineEntry *e,
                     char *err, size_t n);
int  engines_remove(EngineList *l, const char *name);
const EngineEntry *engines_find(const EngineList *l, const char *name);

/* "1s/move", "depth 12", "1s · 1500 Elo" */
void engine_strength_label(const EngineEntry *e, char *buf, size_t n);

#endif
