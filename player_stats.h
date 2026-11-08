#ifndef PLAYER_STATS_H
#define PLAYER_STATS_H

#include <stdbool.h>

/* Everything about the player that a pattern author may want to tune.
   Lives in resources/player.txt so it can be edited without a rebuild. */
typedef struct {
    int   max_hp;    /* hit points (default 100) */
    float speed;     /* move speed, px/s (default 500) */
    float size;      /* draw size and hit radius (default 10) */
    float invuln;    /* seconds of invulnerability after a hit (default 1) */
} PlayerStats;

PlayerStats PlayerStatsDefaults(void);

/* Load; missing file or keys keep the defaults. Returns false with a message
   in err only when the file exists but cannot be read. */
bool PlayerStatsLoad(const char *path, PlayerStats *out, char *err, int errSize);

bool PlayerStatsSave(const char *path, const PlayerStats *s);

#endif
