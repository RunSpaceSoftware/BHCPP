#ifndef PATTERN_SCRIPT_H
#define PATTERN_SCRIPT_H

#include "patterns.h"

/* One bullet a script asked for. Angles are degrees, to match patterns.txt. */
typedef struct { float x, y, angle, speed; } ShotSpec;

#define MAX_SCRIPT_SHOTS 256

/* Run resources/scripts/<p->script>.lua and fill out[] with the bullets its
   spawn(ctx) returns. Returns the count, or -1 with a message in err.
   The lua file defines:  function spawn(ctx) return { {x=,y=,angle=,speed=} } end */
int ScriptSpawn(const Pattern *p, int width, int height, float player_x, float player_y,
                int shot, ShotSpec *out, int maxOut, char *err, int errSize);

void ScriptShutdown(void);

#endif
