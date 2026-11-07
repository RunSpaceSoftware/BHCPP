#ifndef PATTERNS_IO_H
#define PATTERNS_IO_H

#include "patterns.h"

#define PATTERN_NAME_MAX 64

/* A list of patterns together with the storage their names point into.
   Patterns are plain structs, but .name is a pointer - keeping the name
   bytes in a PatternSet means the whole list can be loaded, edited and
   saved without any dynamic allocation. */
typedef struct {
    Pattern items[MAX_PATTERNS];
    char    names[MAX_PATTERNS][PATTERN_NAME_MAX];
} PatternSet;

/* ---------------------------------------------------------------- file I/O
 * Text format (resources/patterns.txt), one block per pattern:
 *
 *   pattern fan5_aimed
 *     origin edge
 *     aim player
 *     count 9
 *     spread 40          # degrees
 *     speed 280
 *     delay 0.4          # seconds between shots
 *     repeat 8           # shots before the next pattern (0 = forever)
 *     damage 10
 *     radius 6
 *     color 255,255,255  # or: yellow, red, orange, ...
 *     sprite bullet_orb  # any png in resources/, without ".png"; none = circle
 *     sprite_size 16     # drawn size in px (default: 2 * radius)
 *     sprite_rot travel  # upright, or travel to face the flight direction
 *   end
 *
 * Angles (spread, spin, jitter, angle) are in degrees, everything else in
 * game units. Any key may be omitted; missing values fall back to the same
 * defaults the C table uses (damage 10, radius 6, yellow, ...). '#' starts a
 * comment. The sprite is only a picture: the hitbox stays the radius, so a
 * big png does not make a bullet easier to hit or harder to dodge. */

/* Load a file. Returns the number of patterns, or -1 on error (message in
   err). Existing contents of out are replaced. */
int PatternsLoad(const char *path, PatternSet *set, char *err, int errSize);

/* Write a list of patterns in the same format. Returns true on success. */
bool PatternsSave(const char *path, const Pattern *list, int count);

/* ------------------------------------------------------- enum <-> text */
const char *OriginName(Origin origin);      /* "edge", "player", ... */
const char *AimName(Aim aim);               /* "player", "wall", ... */
const char *SideName(Side side);            /* "top", ... */
int OriginFromName(const char *name);       /* -1 when unknown */
int AimFromName(const char *name);
int SideFromName(const char *name);

/* ------------------------------------------------------- runtime table */
/* Pattern list the game should use: resources/patterns.txt when it exists,
   otherwise the built-in PATTERNS[] table. Loaded once, then cached. */
const Pattern *PatternsGet(int *count);
const char    *PatternsPath(void);          /* "" when using the built-in table */
bool           PatternsReload(void);        /* re-read from disk */

#endif
