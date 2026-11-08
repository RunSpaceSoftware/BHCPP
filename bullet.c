#include "bullet.h"
#include "pattern_script.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------- sprite cache */
/* Bullets carry a small id instead of a name or a texture handle: the list is
   8192 entries, and headless tools must be able to spawn without a GL context.
   shortcut: 32 distinct sprites per session, raise MAX_SPRITES when a table
   outgrows it (past that the extra names draw as circles). */
#define MAX_SPRITES 32

static char sprite_names[MAX_SPRITES][SPRITE_NAME_MAX];
static Texture2D sprite_tex[MAX_SPRITES];
static bool sprite_loaded[MAX_SPRITES];
static int sprite_count = 0;

/* Registers `name` (bare png name, no extension) and returns its id, -1 if none. */
static int sprite_id(const char *name)
{
    if (!name || !*name) return -1;
    for (int i = 0; i < sprite_count; i++)
        if (strcmp(sprite_names[i], name) == 0) return i;
    if (sprite_count >= MAX_SPRITES) return -1;
    snprintf(sprite_names[sprite_count], SPRITE_NAME_MAX, "%s", name);
    return sprite_count++;
}

/* Loads the png on first use; returns NULL when it is missing. */
static Texture2D *sprite_texture(int id)
{
    if (id < 0 || id >= sprite_count) return NULL;
    if (!sprite_loaded[id]) {
        sprite_loaded[id] = true;   /* try once, so a missing png stays cheap */
        const char *app = GetApplicationDirectory();
        const char *fmt[3] = { "%sresources/%s.png", "%s../resources/%s.png", "%sresources/%s.png" };
        char path[4096];
        for (int i = 0; i < 3 && sprite_tex[id].id == 0; i++) {
            snprintf(path, sizeof path, fmt[i], (i == 2) ? "" : app, sprite_names[id]);
            if (!FileExists(path)) continue;
            sprite_tex[id] = LoadTexture(path);
            if (sprite_tex[id].id != 0) SetTextureFilter(sprite_tex[id], TEXTURE_FILTER_POINT);
        }
    }
    return sprite_tex[id].id != 0 ? &sprite_tex[id] : NULL;
}

/* Drop every cached texture so a png added since start-up is picked up.
   Callers must clear their bullet list first: ids would otherwise dangle. */
void reload_bullet_sprites(void)
{
    for (int i = 0; i < sprite_count; i++) {
        if (sprite_tex[i].id != 0) UnloadTexture(sprite_tex[i]);
        sprite_tex[i].id = 0;
        sprite_loaded[i] = false;
    }
    sprite_count = 0;
}

static void add_bullet_at(BulletList *list, const Pattern *p, float x, float y,
                          float angle, float speed)
{
    if (list->count >= MAX_BULLETS) return;
    Bullet *b = &list->items[list->count++];
    b->x = x;
    b->y = y;
    b->vx = cosf(angle) * speed;
    b->vy = sinf(angle) * speed;
    b->radius = p->radius;
    b->color = p->color;
    b->damage = p->damage;
    b->dead = false;
    b->gravity = p->gravity;
    b->sprite = sprite_id(p->sprite);
    b->sprite_size = p->sprite_size;
    b->sprite_rot = (unsigned char)p->sprite_rot;
}

/* Pattern logic lives in lua: resources/scripts/aims.lua for the built-in
   aims, or the file the pattern names in `script`. This just fires what
   they return. */
static void spawn_one(BulletList *list, const Pattern *p, int width, int height,
                      float player_x, float player_y, SpinState *spin)
{
    ShotSpec specs[MAX_SCRIPT_SHOTS];
    char err[256] = { 0 };
    int n = ScriptSpawn(p, width, height, player_x, player_y, spin->shot,
                        spin->angle, specs, MAX_SCRIPT_SHOTS, err, sizeof err);
    if (n < 0) {
        TraceLog(LOG_WARNING, "PATTERN: %s", err);
        return;
    }
    for (int i = 0; i < n; i++)
        add_bullet_at(list, p, specs[i].x, specs[i].y,
                      specs[i].angle * DEG2RAD, specs[i].speed);
}
void spawn_from_pattern(BulletList *list, const Pattern *p, int width, int height,
                        float player_x, float player_y, SpinState *spin)
{
    spawn_one(list, p, width, height, player_x, player_y, spin);
    spin->angle += p->spin;   /* one spin step per shot, for every aim */
    spin->shot++;             /* alternating aim and scripts count shots */
}

Pattern scale_pattern(const Pattern *pat, DiffScale d)
{
    /* "More bullets" = a bigger count where that keeps the shape sensible
       (rings, fans, rain, spiral arms, formations). Patterns where extra
       bullets would just stack up or close the escape gap (single shots,
       walls, cross, crossfire, spears) fire more *often* instead. */
    Pattern p = pattern_apply_defaults(*pat);
    p.speed = pat->speed * d.speed;
    p.damage = (int)roundf(p.damage * d.damage);
    if (p.damage < 1) p.damage = 1;
    if (d.count != 1.0f) {
        /* A spear is a solid shaft along one line: extra bullets only pack it
           tighter, they do not widen the thing the player has to dodge, so
           they would be difficulty-neutral. Fire the spear again sooner. */
        if (p.count >= 3 && p.aim != AIM_WALL && p.aim != AIM_CROSS
                         && p.aim != AIM_CROSSFIRE && p.aim != AIM_SPEAR) {
            int c = (int)roundf(p.count * d.count);
            p.count = c < 3 ? 3 : c;
        } else {
            p.delay = p.delay / d.count;
        }
    }
    return p;
}

void update_bullets(BulletList *list, float dt, int width, int height)
{
    int i = 0;
    while (i < list->count) {
        Bullet *b = &list->items[i];
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->vy += b->gravity * dt;
        const int m = 50;
        bool off = b->x < -m || b->x > width + m || b->y < -m || b->y > height + m;
        if (b->dead || off) {
            list->items[i] = list->items[list->count - 1];
            list->count--;
        } else {
            i++;
        }
    }
}

void draw_bullets(const BulletList *list)
{
    for (int i = 0; i < list->count; i++) {
        const Bullet *b = &list->items[i];
        Texture2D *tex = sprite_texture(b->sprite);
        if (!tex) {
            /* no png (or none chosen): the circle stays the fallback and the hitbox */
            DrawCircleV((Vector2){ b->x, b->y }, b->radius, b->color);
            continue;
        }
        float size = b->sprite_size > 0.0f ? b->sprite_size : b->radius * 2.0f;
        float rot = (b->sprite_rot == SPRITE_ROT_TRAVEL) ? atan2f(b->vy, b->vx) * RAD2DEG : 0.0f;
        /* fit the longer side to `size` so a non-square png is not squashed */
        float k = size / (float)(tex->width > tex->height ? tex->width : tex->height);
        float dw = tex->width * k, dh = tex->height * k;
        Rectangle src = { 0.0f, 0.0f, (float)tex->width, (float)tex->height };
        Rectangle dst = { b->x, b->y, dw, dh };
        DrawTexturePro(*tex, src, dst, (Vector2){ dw * 0.5f, dh * 0.5f }, rot, WHITE);
    }
}
