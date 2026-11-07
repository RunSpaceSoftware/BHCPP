#ifndef BULLET_H
#define BULLET_H

#include "raylib.h"
#include "patterns.h"
#include <stdbool.h>

#define MAX_BULLETS 8192
#define MARGIN 20
#define BASE_W 1200   /* arena size the patterns were designed for */
#define BASE_H 600

typedef struct {
    float x, y, vx, vy;
    float radius;
    Color color;
    int damage;
    bool dead;        /* set on hit; swept out next update */
    int sprite;       /* index into the sprite name table, -1 = draw a circle */
    float sprite_size;/* drawn size in px */
    unsigned char sprite_rot;   /* SpriteRot */
} Bullet;

typedef struct {
    Bullet items[MAX_BULLETS];
    int count;
} BulletList;

/* Spinning patterns keep their running angle here. */
typedef struct { float angle; } SpinState;

/* Difficulty multipliers (1.0 = unchanged). */
typedef struct { float speed, count, damage; } DiffScale;

/* Append one shot of `pat` (defaults already applied) to the list. */
void spawn_from_pattern(BulletList *list, const Pattern *pat, int width, int height,
                        float player_x, float player_y, SpinState *spin);

/* Copy of pat with defaults applied and the difficulty folded in. */
Pattern scale_pattern(const Pattern *pat, DiffScale d);

void update_bullets(BulletList *list, float dt, int width, int height);
void draw_bullets(const BulletList *list);

/* Forget cached bullet sprites so newly added pngs load; clear the list first. */
void reload_bullet_sprites(void);

#endif