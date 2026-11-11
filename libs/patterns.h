#ifndef PATTERNS_H
#define PATTERNS_H

#include "raylib.h"
#include <stdbool.h>

/* Where a shot spawns. */
typedef enum {
    ORIGIN_EDGE = 0,   /* random point on a random edge */
    ORIGIN_TOP,
    ORIGIN_BOTTOM,
    ORIGIN_LEFT,
    ORIGIN_RIGHT,
    ORIGIN_POINT,      /* fixed x,y (see .point) */
    ORIGIN_PLAYER      /* player position */
} Origin;

/* How the bullets are aimed / laid out. */
typedef enum {
    AIM_PLAYER = 0,    /* toward the player */
    AIM_ANGLE,         /* fixed angle, or spinning if .spin is set */
    AIM_RADIAL,        /* away from origin; spread == 2*PI gives a full ring */
    AIM_WALL,          /* a line of bullets crossing the arena */
    AIM_RAIN,          /* random spots along the side named by .origin */
    AIM_CROSSFIRE,     /* one aimed shot from a random spot on each side */
    AIM_ENCIRCLE,      /* ring around the player, all converging inward */
    AIM_CROSS,         /* bullets around the player on 4 sides, converging */
    AIM_SPEAR,         /* long narrow piercing shot */
    AIM_FORMATION,     /* complex geometric grouping */
    AIM_ALTERNATE      /* wall that flips between opposite sides each shot */
} Aim;

typedef enum { SIDE_TOP = 0, SIDE_BOTTOM, SIDE_LEFT, SIDE_RIGHT } Side;

/* .gap values: 0 = no gap, GAP_RANDOM = random slot, GAP_AT(i) = slot i. */
#define GAP_RANDOM (-1)
#define GAP_AT(i)  ((i) + 1)

/* Unset (zero) fields get these defaults in pattern_apply_defaults(). */
#define DEFAULT_DAMAGE 10
#define DEFAULT_RADIUS 6.0f

/* Warning shown before a pattern starts firing (see pattern_warn_seconds). */
#define DEFAULT_WARN        1.0f
#define DEFAULT_WARN_RADIUS 130.0f

#define SPRITE_NAME_MAX 64
#define SOUND_NAME_MAX 64
#define SCRIPT_NAME_MAX 32

/* How a bullet sprite is turned when drawn. */
typedef enum {
    SPRITE_ROT_NONE = 0,   /* drawn upright */
    SPRITE_ROT_TRAVEL      /* turned to face the direction of travel */
} SpriteRot;

typedef struct {
    const char *name;
    Origin origin;
    Aim aim;
    Side side;              /* walls only */
    Vector2 point;          /* ORIGIN_POINT only (designed for a 1200x600 arena) */
    int count;              /* bullets per shot (default 1) */
    float spread;           /* total fan angle, radians */
    float speed;            /* px/s */
    float delay;            /* seconds before the NEXT shot */
    int repeat;             /* shots before the next pattern (0 = forever) */
    int damage;             /* default 10 */
    float radius;           /* default 6 */
    Color color;            /* default YELLOW (alpha 0 = unset) */
    float spin;             /* radians added to the angle each shot */
    float jitter;           /* radians of random spread per bullet */
    bool has_angle;         /* .angle is meaningful */
    float angle;
    int gap;                /* walls / encircle: see GAP_* above */
    float spawn_radius;     /* encircle / cross (default 220) */
    float min_radius;       /* encircle / cross (default 70) */
    int segments;           /* spear / formation (default 3) */
    float length;           /* spear / formation (default 90) */
    char sprite[SPRITE_NAME_MAX]; /* png in resources/, "" = draw a circle */
    float sprite_size;      /* drawn size in px (default: 2*radius) */
    SpriteRot sprite_rot;   /* how the sprite is turned when drawn */
    float gravity;          /* px/s^2 pulling the bullet down (0 = straight) */
    char script[SCRIPT_NAME_MAX]; /* lua file in resources/scripts, "" = none */
    char sound[SOUND_NAME_MAX];   /* sound file in resources/, played on every
                                     shot of this pattern; "" = silent */
    float warn;             /* seconds of warning before the first shot:
                               0 = default, > 0 = that long, < 0 = none.
                               Read it through pattern_warn_seconds(). */
    float warn_radius;      /* size of the warning circle, px in the 1200x600
                               arena (default 130, scales with the window) */
} Pattern;

#define MAX_PATTERNS 64

extern const Pattern PATTERNS[];
extern const int PATTERN_COUNT;

/* Returns p with defaults filled in. */
Pattern pattern_apply_defaults(Pattern p);

/* Seconds the game warns the player before this pattern's first shot.
   Patterns that fire from a fixed point (rings, spirals, lattices) are lethal
   if you are standing on it when they start, so they warn for DEFAULT_WARN
   unless `warn` says otherwise; every other pattern defaults to no warning. */
float pattern_warn_seconds(const Pattern *p);

#endif