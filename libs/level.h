#ifndef LEVEL_H
#define LEVEL_H

#include <stdbool.h>

/* Waves of pattern names. The engine's level format is deliberately tiny:
     level "Warm Up"
       pattern fan5_aimed
       pattern wall_gap
     end
   The game maps names to its live pattern table, the editor shows the list.
   A missing levels.txt falls back to one level containing every pattern. */

#define LEVEL_MAX_LEVELS   16
#define LEVEL_MAX_PATTERNS 24
#define LEVEL_NAME_MAX     64
#define LEVEL_PATTERN_MAX  64

typedef struct {
    char name[LEVEL_NAME_MAX];
    char patterns[LEVEL_MAX_PATTERNS][LEVEL_PATTERN_MAX];
    int pattern_count;
} Level;

typedef struct {
    Level items[LEVEL_MAX_LEVELS];
    int count;
} LevelSet;

void LevelsClear(LevelSet *set);

/* One level called "All Patterns" holding the names supplied (up to the
   format limit). */
bool LevelsDefault(LevelSet *set, const char *const *pattern_names, int count);

bool LevelsLoad(const char *path, LevelSet *set, char *err, int errSize);
bool LevelsSave(const char *path, const LevelSet *set);
int  LevelsFind(const LevelSet *set, const char *name);

#endif
