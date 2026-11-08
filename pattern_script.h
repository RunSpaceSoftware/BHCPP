#ifndef PATTERN_SCRIPT_H
#define PATTERN_SCRIPT_H

#include "patterns.h"

/* One bullet a script asked for. Angles are degrees, to match patterns.txt. */
typedef struct { float x, y, angle, speed; } ShotSpec;

#define MAX_SCRIPT_SHOTS 512

/* Run the pattern's spawn logic and fill out[] with the bullets it returns.
   A pattern with `script` set runs resources/scripts/<script>.lua; otherwise
   its aim runs from resources/scripts/aims.lua. Returns the count, or -1 with
   a message in err. spin_now is the accumulated spin angle in radians. */
int ScriptSpawn(const Pattern *p, int width, int height, float player_x, float player_y,
                int shot, float spin_now, ShotSpec *out, int maxOut, char *err, int errSize);

/* Drop cached scripts so edited .lua files are re-read. */
void ScriptReload(void);

void ScriptShutdown(void);

#endif
