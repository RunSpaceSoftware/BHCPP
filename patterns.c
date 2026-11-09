#include "patterns.h"
#include <math.h>

#define D2R(deg) ((deg) * PI / 180.0f)

/* Same patterns as patterns.py. Fields left out take the defaults.
 *
 * NOTE: these are the built-in defaults. At start-up the game first looks for
 * resources/patterns.txt and uses that list when it exists, so patterns can be
 * tweaked (by hand or with tools/pattern_editor.exe) without recompiling.
 * This table is what the game falls back to when the file is missing. */
const Pattern PATTERNS[] = {
    { .name = "single_aimed", .origin = ORIGIN_EDGE, .aim = AIM_PLAYER,
      .count = 1, .spread = 0, .speed = 300, .delay = 0.15f, .repeat = 20 },

    { .name = "fan5_aimed", .origin = ORIGIN_EDGE, .aim = AIM_PLAYER,
      .count = 9, .spread = D2R(40), .speed = 280, .delay = 0.4f, .repeat = 8 },

    { .name = "ring32", .origin = ORIGIN_POINT, .point = {600, 300},
      .aim = AIM_RADIAL, .count = 32, .spread = 2 * PI, .speed = 220,
      .delay = 0.2f, .repeat = 24, .spin = 0.2f,
      .sprite = "bullet_orb", .sprite_size = 16 },

    { .name = "spiral", .origin = ORIGIN_POINT, .point = {600, 300},
      .aim = AIM_ANGLE, .count = 2, .spread = 0, .speed = 360,
      .delay = 0.04f, .repeat = 192, .spin = 1.3f },

    /* two counter-rotating arms sweeping the arena */
    { .name = "spiral_double", .origin = ORIGIN_POINT, .point = {600, 300},
      .aim = AIM_ANGLE, .count = 4, .spread = 0, .speed = 300,
      .delay = 0.05f, .repeat = 150, .spin = -0.7f,
      .color = {255, 165, 0, 255} },

    /* slow fat bullets, hard to dodge but easy to see */
    { .name = "lattice", .origin = ORIGIN_POINT, .point = {600, 300},
      .aim = AIM_RADIAL, .count = 16, .spread = 2 * PI, .speed = 140,
      .delay = 0.5f, .repeat = 10, .spin = 0.11f, .radius = 12,
      .color = {120, 200, 255, 255}, .damage = 20,
      .sprite = "bullet_hex", .sprite_size = 26, .sprite_rot = SPRITE_ROT_TRAVEL },

    /* a curtain of bullets with one gap to thread through */
    { .name = "wall_gap", .origin = ORIGIN_TOP, .aim = AIM_WALL,
      .side = SIDE_TOP, .count = 18, .gap = GAP_RANDOM, .speed = 260,
      .delay = 0.9f, .repeat = 8, .color = {200, 120, 255, 255}, .damage = 8 },

    /* dense wall, one fixed hole, slower speed */
    { .name = "wall_solid", .origin = ORIGIN_LEFT, .aim = AIM_WALL,
      .side = SIDE_LEFT, .count = 22, .gap = GAP_RANDOM, .speed = 200,
      .delay = 1.1f, .repeat = 6, .color = {255, 120, 120, 255}, .damage = 12 },

    /* random spray coming in from the bottom */
    { .name = "rain", .origin = ORIGIN_BOTTOM, .aim = AIM_RAIN,
      .count = 6, .jitter = D2R(18), .speed = 250, .delay = 0.2f,
      .repeat = 30, .color = {150, 255, 150, 255} },

    /* bullets fired from both sides at once, always tracking the player */
    { .name = "crossfire", .origin = ORIGIN_LEFT, .aim = AIM_CROSSFIRE,
      .count = 2, .speed = 320, .delay = 0.45f, .repeat = 14,
      .color = {255, 220, 120, 255}, .damage = 8 },

    /* fast aimed shot with random jitter */
    { .name = "aimed_jitter", .origin = ORIGIN_EDGE, .aim = AIM_PLAYER,
      .count = 3, .spread = D2R(12), .jitter = D2R(6), .speed = 380,
      .delay = 0.25f, .repeat = 18, .color = {255, 90, 90, 255}, .damage = 15 },

    /* a ring closes in from every side at once */
    { .name = "encircle", .origin = ORIGIN_PLAYER, .aim = AIM_ENCIRCLE,
      .count = 12, .spawn_radius = 260, .speed = 180, .delay = 1.0f,
      .repeat = 5, .spin = 0.4f, .color = {255, 140, 255, 255}, .damage = 12,
      .sprite = "bullet_ring", .sprite_size = 18 },

    /* 4 arms pinching in from up/down/left/right */
    { .name = "cross_close", .origin = ORIGIN_PLAYER, .aim = AIM_CROSS,
      .count = 4, .spawn_radius = 280, .speed = 240, .delay = 0.55f,
      .repeat = 8, .spin = PI / 4, .color = {120, 240, 255, 255}, .damage = 8 },

    /* ---------------- added patterns ---------------- */
    { .name = "rain_down", .origin = ORIGIN_TOP, .aim = AIM_RAIN,
      .count = 7, .jitter = D2R(14), .speed = 260, .delay = 0.2f,
      .repeat = 30, .color = {120, 200, 255, 255}, .damage = 8 },

    { .name = "rain_side", .origin = ORIGIN_LEFT, .aim = AIM_RAIN,
      .count = 5, .jitter = D2R(12), .speed = 280, .delay = 0.2f,
      .repeat = 30, .color = {255, 200, 120, 255}, .damage = 8 },

    { .name = "wall_bottom_gap", .origin = ORIGIN_BOTTOM, .aim = AIM_WALL,
      .side = SIDE_BOTTOM, .count = 20, .gap = GAP_RANDOM, .speed = 240,
      .delay = 0.9f, .repeat = 8, .color = {255, 160, 90, 255}, .damage = 8 },

    { .name = "wall_right_gap", .origin = ORIGIN_RIGHT, .aim = AIM_WALL,
      .side = SIDE_RIGHT, .count = 12, .gap = GAP_RANDOM, .speed = 250,
      .delay = 0.9f, .repeat = 8, .color = {160, 255, 200, 255}, .damage = 8 },

    { .name = "fan_bottom", .origin = ORIGIN_BOTTOM, .aim = AIM_PLAYER,
      .count = 7, .spread = D2R(50), .speed = 270, .delay = 0.45f,
      .repeat = 10, .color = {255, 220, 120, 255}, .damage = 8 },

    { .name = "burst_right", .origin = ORIGIN_RIGHT, .aim = AIM_PLAYER,
      .count = 5, .spread = D2R(25), .jitter = D2R(3), .speed = 330,
      .delay = 0.3f, .repeat = 14, .color = {255, 120, 180, 255}, .damage = 10 },

    { .name = "bottom_volley", .origin = ORIGIN_BOTTOM, .aim = AIM_ANGLE,
      .count = 1, .speed = 300, .delay = 0.08f, .repeat = 50,
      .color = {255, 255, 160, 255}, .damage = 8 },

    { .name = "ring_left", .origin = ORIGIN_POINT, .point = {300, 300},
      .aim = AIM_RADIAL, .count = 20, .spread = 2 * PI, .speed = 200,
      .delay = 0.35f, .repeat = 10, .spin = 0.15f,
      .color = {255, 150, 200, 255}, .damage = 8 },

    { .name = "ring_right", .origin = ORIGIN_POINT, .point = {900, 300},
      .aim = AIM_RADIAL, .count = 20, .spread = 2 * PI, .speed = 200,
      .delay = 0.35f, .repeat = 10, .spin = -0.15f,
      .color = {150, 200, 255, 255}, .damage = 8 },

    { .name = "spiral_triple", .origin = ORIGIN_POINT, .point = {600, 300},
      .aim = AIM_ANGLE, .count = 3, .speed = 280, .delay = 0.06f,
      .repeat = 120, .spin = 0.9f, .color = {180, 255, 150, 255}, .damage = 8 },

    { .name = "encircle_gap", .origin = ORIGIN_PLAYER, .aim = AIM_ENCIRCLE,
      .count = 16, .spawn_radius = 240, .gap = GAP_RANDOM, .speed = 200,
      .delay = 0.8f, .repeat = 6, .spin = 0.3f,
      .color = {255, 180, 255, 255}, .damage = 10 },

    { .name = "crossfire_fast", .origin = ORIGIN_LEFT, .aim = AIM_CROSSFIRE,
      .count = 2, .speed = 400, .delay = 0.35f, .repeat = 16,
      .color = {255, 240, 140, 255}, .damage = 8 },

    /* a shaft of bullets: one piercing thrust from the side */
    { .name = "spear_thrust", .origin = ORIGIN_LEFT, .aim = AIM_SPEAR,
      .count = 6, .length = 120, .speed = 420, .delay = 0.7f, .repeat = 8,
      .color = {255, 255, 255, 255}, .damage = 12,
      .sprite = "bullet_spear", .sprite_size = 26, .sprite_rot = SPRITE_ROT_TRAVEL },

    /* the same shaft but pinned at 90 degrees and slowly rotating */
    { .name = "spear_lance", .origin = ORIGIN_POINT, .point = {600, 300},
      .aim = AIM_SPEAR, .count = 8, .length = 160, .speed = 300,
      .delay = 0.5f, .repeat = 12, .spin = D2R(9),
      .has_angle = true, .angle = D2R(90),
      .color = {150, 200, 255, 255}, .damage = 10,
      .sprite = "bullet_spear", .sprite_size = 34, .sprite_rot = SPRITE_ROT_TRAVEL },

    /* flower: six vertices around the player, each firing a small fan */
    { .name = "formation_flower", .origin = ORIGIN_PLAYER, .aim = AIM_FORMATION,
      .segments = 6, .count = 2, .spread = D2R(20), .length = 140,
      .speed = 200, .delay = 0.9f, .repeat = 6, .spin = D2R(5),
      .sprite = "bullet_star", .sprite_size = 18, .color = {255, 140, 255, 255} },

    /* pincer: three vertices from the left, converging on the player */
    { .name = "formation_pincer", .origin = ORIGIN_LEFT, .aim = AIM_FORMATION,
      .segments = 3, .count = 3, .spread = D2R(18), .length = 100,
      .speed = 260, .delay = 0.8f, .repeat = 8,
      .color = {120, 240, 255, 255} },

    /* ---- gravity shots: shells that arc and fall to the bottom ---- */
    { .name = "cannon_lob", .origin = ORIGIN_TOP, .aim = AIM_ANGLE,
      .count = 3, .spread = D2R(14), .speed = 360, .delay = 0.9f, .repeat = 8,
      .has_angle = true, .angle = D2R(70), .gravity = 520,
      .sprite = "bullet_orb", .sprite_size = 16,
      .color = {255, 170, 90, 255}, .damage = 12 },

    /* dropped straight down, speeding up as it falls */
    { .name = "cannon_drop", .origin = ORIGIN_TOP, .aim = AIM_RAIN,
      .count = 5, .jitter = D2R(10), .speed = 120, .delay = 0.7f, .repeat = 10,
      .gravity = 420, .sprite = "bullet_hex", .sprite_size = 18,
      .color = {255, 120, 120, 255} },

    /* the same wall from the other side, every other shot */
    { .name = "alternate_walls", .origin = ORIGIN_LEFT, .aim = AIM_ALTERNATE,
      .count = 14, .gap = GAP_RANDOM, .speed = 260,
      .delay = 0.85f, .repeat = 10, .color = {120, 240, 255, 255}, .damage = 10 },

    /* everything decided by resources/scripts/cannon.lua */
    { .name = "script_mortar", .origin = ORIGIN_TOP, .aim = AIM_ANGLE,
      .count = 5, .speed = 420, .delay = 0.8f, .repeat = 12,
      .gravity = 560, .script = "cannon",
      .sprite = "bullet_star", .sprite_size = 16,
      .color = {255, 220, 120, 255} },
};

const int PATTERN_COUNT = (int)(sizeof(PATTERNS) / sizeof(PATTERNS[0]));

Pattern pattern_apply_defaults(Pattern p)
{
    if (p.count <= 0)        p.count = 1;
    if (p.damage <= 0)       p.damage = DEFAULT_DAMAGE;
    if (p.radius <= 0)       p.radius = DEFAULT_RADIUS;
    if (p.color.a == 0)      p.color = YELLOW;
    if (p.spawn_radius <= 0) p.spawn_radius = 220;
    if (p.min_radius <= 0)   p.min_radius = 70;
    if (p.segments <= 0)     p.segments = 3;
    if (p.length <= 0)       p.length = 90.0f;
    if (p.sprite_size <= 0)  p.sprite_size = p.radius * 2.0f;
    if (p.warn_radius <= 0)  p.warn_radius = DEFAULT_WARN_RADIUS;
    p.sprite[SPRITE_NAME_MAX - 1] = '\0';
    return p;
}

float pattern_warn_seconds(const Pattern *p)
{
    if (p->warn > 0.0f) return p->warn;
    if (p->warn < 0.0f) return 0.0f;
    return (p->origin == ORIGIN_POINT) ? DEFAULT_WARN : 0.0f;
}
