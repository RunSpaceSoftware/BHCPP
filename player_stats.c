#include "player_stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PlayerStats PlayerStatsDefaults(void)
{
    PlayerStats s;
    s.max_hp = 100;
    s.speed = 500.0f;
    s.size = 10.0f;
    s.invuln = 1.0f;
    return s;
}

bool PlayerStatsLoad(const char *path, PlayerStats *out, char *err, int errSize)
{
    if (!out) return false;
    *out = PlayerStatsDefaults();
    if (!path) return true;

    FILE *f = fopen(path, "rb");
    if (!f) return true;   /* absent file simply means "all defaults" */

    char line[512];
    int lineNo = 0;
    while (fgets(line, sizeof line, f)) {
        lineNo++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char key[64] = { 0 };
        char value[64] = { 0 };
        if (sscanf(line, " %63s %63s", key, value) != 2) continue;

        float v = (float)atof(value);
        if (strcmp(key, "hp") == 0) {
            if (v < 1) goto bad;
            out->max_hp = (int)v;
        } else if (strcmp(key, "speed") == 0) {
            if (v <= 0) goto bad;
            out->speed = v;
        } else if (strcmp(key, "size") == 0) {
            if (v <= 0) goto bad;
            out->size = v;
        } else if (strcmp(key, "invuln") == 0) {
            if (v < 0) goto bad;
            out->invuln = v;
        } else {
            if (err) snprintf(err, errSize, "line %d: unknown key '%s'", lineNo, key);
            fclose(f);
            return false;
        }
        continue;
bad:
        if (err) snprintf(err, errSize, "line %d: bad value '%s' for '%s'", lineNo, value, key);
        fclose(f);
        return false;
    }

    fclose(f);
    return true;
}

bool PlayerStatsSave(const char *path, const PlayerStats *s)
{
    if (!path || !s) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    fprintf(f, "# Bullet Hell player stats\n");
    fprintf(f, "# Written by tools/pattern_editor - you can also edit this by hand.\n\n");
    fprintf(f, "hp %d\n", s->max_hp);
    fprintf(f, "speed %g\n", s->speed);
    fprintf(f, "size %g\n", s->size);
    fprintf(f, "invuln %g\n", s->invuln);

    fclose(f);
    return true;
}
