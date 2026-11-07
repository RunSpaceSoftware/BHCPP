#include "bullet.h"
#include <math.h>

#define TAU (2.0f * PI)

static float clampf(float v, float lo, float hi)
{
    float r = (v < lo) ? lo : v;
    return (r > hi) ? hi : r;
}

/* uniform float in [a, b] */
static float rand_range(float a, float b)
{
    return a + (b - a) * ((float)GetRandomValue(0, 100000) / 100000.0f);
}

static void add_bullet(BulletList *list, const Pattern *p, float x, float y, float angle)
{
    if (list->count >= MAX_BULLETS) return;
    Bullet *b = &list->items[list->count++];
    b->x = x;
    b->y = y;
    b->vx = cosf(angle) * p->speed;
    b->vy = sinf(angle) * p->speed;
    b->radius = p->radius;
    b->color = p->color;
    b->damage = p->damage;
    b->dead = false;
}

static void edge_point(int width, int height, float *x, float *y)
{
    switch (GetRandomValue(0, 3)) {
        case 0:  *x = rand_range(0, width);  *y = -MARGIN;          break;
        case 1:  *x = width + MARGIN;        *y = rand_range(0, height); break;
        case 2:  *x = rand_range(0, width);  *y = height + MARGIN;  break;
        default: *x = -MARGIN;               *y = rand_range(0, height); break;
    }
}

static void origin_point(const Pattern *p, int width, int height,
                         float player_x, float player_y, float *x, float *y)
{
    switch (p->origin) {
        case ORIGIN_EDGE:   edge_point(width, height, x, y); break;
        case ORIGIN_TOP:    *x = rand_range(0, width);  *y = -MARGIN; break;
        case ORIGIN_BOTTOM: *x = rand_range(0, width);  *y = height + MARGIN; break;
        case ORIGIN_LEFT:   *x = -MARGIN;               *y = rand_range(0, height); break;
        case ORIGIN_RIGHT:  *x = width + MARGIN;        *y = rand_range(0, height); break;
        case ORIGIN_POINT:
            /* point coords are written for BASE_W x BASE_H; scale them so a
               fixed point stays put relative to the window when it resizes */
            *x = p->point.x * width  / BASE_W;
            *y = p->point.y * height / BASE_H;
            break;
        default:            *x = player_x; *y = player_y; break;
    }
}

/* resolve .gap into a slot index in [0, n), or -1 for "no gap" */
static int resolve_gap(int gap, int n)
{
    if (gap == GAP_RANDOM) return GetRandomValue(0, n - 1);
    if (gap > 0) return gap - 1;
    return -1;
}

static void spawn_wall(BulletList *list, const Pattern *p, int width, int height)
{
    int n = p->count;
    int gap = resolve_gap(p->gap, n);
    for (int i = 0; i < n; i++) {
        if (i == gap) continue;
        float t = (i + 0.5f) / n;
        float x, y, a;
        switch (p->side) {
            case SIDE_TOP:    x = width * t;        y = -MARGIN;         a = PI / 2;  break;
            case SIDE_BOTTOM: x = width * t;        y = height + MARGIN; a = -PI / 2; break;
            case SIDE_LEFT:   x = -MARGIN;          y = height * t;      a = 0.0f;    break;
            default:          x = width + MARGIN;   y = height * t;      a = PI;      break;
        }
        add_bullet(list, p, x, y, p->has_angle ? p->angle : a);
    }
}

static void spawn_rain(BulletList *list, const Pattern *p, int width, int height)
{
    /* bullets come in from the side named by .origin; anything else = top */
    Origin side = p->origin;
    float def;
    switch (side) {
        case ORIGIN_BOTTOM: def = -PI / 2; break;
        case ORIGIN_LEFT:   def = 0.0f;    break;
        case ORIGIN_RIGHT:  def = PI;      break;
        default: side = ORIGIN_TOP; def = PI / 2; break;
    }
    float dir = p->has_angle ? p->angle : def;
    for (int i = 0; i < p->count; i++) {
        float x, y;
        switch (side) {
            case ORIGIN_TOP:    x = rand_range(0, width);  y = -MARGIN; break;
            case ORIGIN_BOTTOM: x = rand_range(0, width);  y = height + MARGIN; break;
            case ORIGIN_LEFT:   x = -MARGIN;               y = rand_range(0, height); break;
            default:            x = width + MARGIN;        y = rand_range(0, height); break;
        }
        add_bullet(list, p, x, y, dir + rand_range(-p->jitter, p->jitter));
    }
}

static void spawn_crossfire(BulletList *list, const Pattern *p, int width, int height,
                            float player_x, float player_y)
{
    float xs[2] = { -MARGIN, width + MARGIN };
    for (int i = 0; i < 2; i++) {
        float y = rand_range(0, height);
        add_bullet(list, p, xs[i], y, atan2f(player_y - y, player_x - xs[i]));
    }
}

/* Spawn on a ring, then converge on the player. The ring's *center* is
   clamped so the whole ring fits on screen - clamping each point instead
   would distort the cross arms near an edge. */
static void spawn_ring_in(BulletList *list, const Pattern *p, int width, int height,
                          float player_x, float player_y, SpinState *spin)
{
    float r = p->spawn_radius;
    float cx = clampf(player_x, MARGIN + r, width  - MARGIN - r);
    float cy = clampf(player_y, MARGIN + r, height - MARGIN - r);
    float offset = p->has_angle ? p->angle : 0.0f;
    if (p->spin != 0) {
        offset += spin->angle;
        spin->angle += p->spin;
    }
    int gap = resolve_gap(p->gap, p->count);
    for (int i = 0; i < p->count; i++) {
        if (i == gap) continue;
        float a = offset + TAU * i / p->count;
        float x = cx + cosf(a) * r;
        float y = cy + sinf(a) * r;
        if (hypotf(x - player_x, y - player_y) < p->min_radius)
            continue;   /* on top of the player: leave this side open */
        add_bullet(list, p, x, y, atan2f(player_y - y, player_x - x));
    }
}

/* The angle a shot starts from: at the player, a fixed/spinning angle, or
   outward from the arena centre. Shared by the plain fan and the newer
   spear/formation shapes so all of them read .has_angle/.spin the same way. */
static float base_angle(const Pattern *p, float ox, float oy, int width, int height,
                        float player_x, float player_y, SpinState *spin)
{
    if (p->aim == AIM_PLAYER) {
        return atan2f(player_y - oy, player_x - ox);
    }
    if (p->aim == AIM_ANGLE) {
        if (!p->has_angle && p->spin == 0) {
            /* from a side origin with no angle given: head inward */
            switch (p->origin) {
                case ORIGIN_TOP:    return PI / 2;
                case ORIGIN_BOTTOM: return -PI / 2;
                case ORIGIN_LEFT:   return 0.0f;
                case ORIGIN_RIGHT:  return PI;
                default:            return 0.0f;
            }
        }
        if (p->spin != 0) {
            float a = spin->angle;
            spin->angle += p->spin;
            return a;
        }
        return p->angle;
    }
    if (p->aim == AIM_RADIAL) {
        return atan2f(oy - height / 2.0f, ox - width / 2.0f);
    }
    return 0.0f;
}

/* Rotates a shape by .spin, one step per shot. base_angle() only advances the
   spin for AIM_ANGLE, so the newer shapes do it themselves. */
static float apply_spin(float base, const Pattern *p, SpinState *spin)
{
    if (p->spin == 0) return base;
    float a = base + spin->angle;
    spin->angle += p->spin;
    return a;
}

/* Heading for the spear/formation shapes: an explicit .angle if the pattern
   set one, otherwise straight at the player. */
static float shape_heading(const Pattern *p, float ox, float oy,
                           float player_x, float player_y)
{
    if (p->has_angle) return p->angle;
    return atan2f(player_y - oy, player_x - ox);
}

/* A long, thin column of bullets travelling together: one "spear" that reads
   as a single piercing shot instead of a spread. .count is how many bullets
   make up the shaft and .length how far the shaft stretches, so the head
   reaches the player while the tail is still on its way in. */
static void spawn_spear(BulletList *list, const Pattern *p, int width, int height,
                        float player_x, float player_y, SpinState *spin)
{
    float ox, oy;
    origin_point(p, width, height, player_x, player_y, &ox, &oy);

    float base = apply_spin(shape_heading(p, ox, oy, player_x, player_y), p, spin);

    int n = p->count;
    if (n < 1) n = 1;
    if (n > 256) n = 256;

    float step = (n > 1) ? p->length / (float)(n - 1) : 0.0f;
    float dx = cosf(base) * step;
    float dy = sinf(base) * step;

    for (int i = 0; i < n; i++) {
        /* bullets sit behind the head, spaced along the shaft */
        float x = ox - dx * i;
        float y = oy - dy * i;
        float a = base;
        if (p->spread != 0 && n > 1)
            a += p->spread * ((float)i / (float)(n - 1) - 0.5f);
        if (p->jitter != 0) a += rand_range(-p->jitter, p->jitter);
        add_bullet(list, p, x, y, a);
    }
}

/* A geometric group: .segments vertices are placed on a ring of radius
   .length around the origin, and each vertex fires .count bullets fanned by
   .spread. Good for flower/pincer shapes. If the pattern pins .angle every
   vertex fires that way, otherwise each vertex aims at the player. */
static void spawn_formation(BulletList *list, const Pattern *p, int width, int height,
                            float player_x, float player_y, SpinState *spin)
{
    float ox, oy;
    origin_point(p, width, height, player_x, player_y, &ox, &oy);

    int segs = p->segments;
    if (segs < 1) segs = 1;
    if (segs > 64) segs = 64;
    int n = p->count;
    if (n < 1) n = 1;
    if (n > 64) n = 64;

    bool aimed = !p->has_angle;
    float pinned = shape_heading(p, ox, oy, player_x, player_y);
    float ring = p->length;

    for (int s = 0; s < segs; s++) {
        float va = apply_spin(TAU * (float)s / (float)segs, p, spin);
        float vx = ox + cosf(va) * ring;
        float vy = oy + sinf(va) * ring;

        float aim = aimed ? atan2f(player_y - vy, player_x - vx) : pinned;

        for (int i = 0; i < n; i++) {
            float a = aim;
            if (n > 1)
                a += p->spread * ((float)i / (float)(n - 1) - 0.5f);
            if (p->jitter != 0) a += rand_range(-p->jitter, p->jitter);
            add_bullet(list, p, vx, vy, a);
        }
    }
}

void spawn_from_pattern(BulletList *list, const Pattern *p, int width, int height,
                        float player_x, float player_y, SpinState *spin)
{
    switch (p->aim) {
        case AIM_WALL:      spawn_wall(list, p, width, height); return;
        case AIM_RAIN:      spawn_rain(list, p, width, height); return;
        case AIM_CROSSFIRE: spawn_crossfire(list, p, width, height, player_x, player_y); return;
        case AIM_ENCIRCLE:
        case AIM_CROSS:     spawn_ring_in(list, p, width, height, player_x, player_y, spin); return;
        case AIM_SPEAR:     spawn_spear(list, p, width, height, player_x, player_y, spin); return;
        case AIM_FORMATION: spawn_formation(list, p, width, height, player_x, player_y, spin); return;
        default: break;
    }

    float ox, oy;
    origin_point(p, width, height, player_x, player_y, &ox, &oy);

    /* base angle */
    float base = base_angle(p, ox, oy, width, height, player_x, player_y, spin);

    /* the set of angles for this shot */
    int n = p->count;
    float angles[256];
    if (n > 256) n = 256;

    if (p->aim == AIM_RADIAL && fabsf(p->spread - TAU) < 1e-4f) {
        float offset = 0.0f;
        if (p->spin != 0) {
            offset = spin->angle;
            spin->angle += p->spin;
        }
        for (int i = 0; i < n; i++) angles[i] = TAU * i / n + offset;
    } else if (p->aim == AIM_ANGLE && p->spread == 0 && n > 1) {
        /* multiple arms, evenly spaced around the origin */
        for (int i = 0; i < n; i++) angles[i] = base + TAU * i / n;
    } else if (n == 1) {
        angles[0] = base;
    } else {
        for (int i = 0; i < n; i++)
            angles[i] = base + p->spread * ((float)i / (n - 1) - 0.5f);
    }

    for (int i = 0; i < n; i++) {
        float a = angles[i];
        if (p->jitter != 0) a += rand_range(-p->jitter, p->jitter);
        add_bullet(list, p, ox, oy, a);
    }
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
        DrawCircleV((Vector2){ b->x, b->y }, b->radius, b->color);
    }
}