/* Headless sanity check for the spear and formation shapes.
 * Build:  mingw32-make PLATFORM=PLATFORM_DESKTOP shapetest
 * Run:    tools\shape_test.exe
 *
 * No window is created - GetRandomValue() works without one - so this can run
 * on a machine with no display. It checks the geometric invariants that are
 * easy to break when the spawn code is edited:
 *
 *   spear      bullets lie on one line through the origin, spaced .length
 *              apart end to end, all moving along that line
 *   formation  .segments vertices on a ring of radius .length, each vertex
 *              holding .count bullets
 */
#include "bullet.h"
#include "patterns.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(bool cond, const char *what)
{
    printf("%s %s\n", cond ? "  ok  " : "  FAIL", what);
    if (!cond) failures++;
}

static bool near(float a, float b, float tol)
{
    return fabsf(a - b) <= tol;
}

/* heading of a bullet, in radians */
static float heading(const Bullet *b)
{
    return atan2f(b->vy, b->vx);
}

/* smallest angle between two headings, in [0, PI] - a raw subtraction wraps
   when the fan straddles the +/-PI seam */
static float heading_gap(float a, float b)
{
    float d = a - b;
    while (d > PI) d -= 2.0f * PI;
    while (d < -PI) d += 2.0f * PI;
    return fabsf(d);
}

static void test_spear(void)
{
    printf("spear:\n");
    Pattern p = { 0 };
    p.name = "t";
    p.origin = ORIGIN_LEFT;
    p.aim = AIM_SPEAR;
    p.count = 6;
    p.length = 120;
    p.speed = 400;
    p.radius = 6;
    p.color = YELLOW;
    p = pattern_apply_defaults(p);

    BulletList list;
    list.count = 0;
    SpinState spin = { 0 };
    spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);

    check(list.count == 6, "6 shaft bullets spawned");

    /* every bullet heads at the player (no .angle set) */
    float want = atan2f(300 - list.items[0].y, 600 - list.items[0].x);
    bool same_heading = true;
    for (int i = 0; i < list.count; i++)
        if (!near(heading(&list.items[i]), want, 1e-3f)) same_heading = false;
    check(same_heading, "all bullets travel along the shaft direction");

    /* consecutive bullets are .length/(count-1) apart, collinear */
    float step = p.length / (float)(p.count - 1);
    bool spaced = true, collinear = true;
    for (int i = 1; i < list.count; i++) {
        float d = hypotf(list.items[i].x - list.items[i - 1].x,
                         list.items[i].y - list.items[i - 1].y);
        if (!near(d, step, 0.1f)) spaced = false;
    }
    /* cross product of (head - tail) with every point's offset stays ~0 */
    float ax = list.items[0].x - list.items[list.count - 1].x;
    float ay = list.items[0].y - list.items[list.count - 1].y;
    for (int i = 0; i < list.count; i++) {
        float bx = list.items[i].x - list.items[list.count - 1].x;
        float by = list.items[i].y - list.items[list.count - 1].y;
        if (fabsf(ax * by - ay * bx) > 1.0f) collinear = false;
    }
    check(spaced, "bullets are evenly spaced along the shaft");
    check(collinear, "bullets are collinear");

    /* total shaft length matches .length */
    float span = hypotf(ax, ay);
    check(near(span, p.length, 0.5f), "shaft span equals .length");

    /* a single-bullet spear must not divide by zero */
    Pattern one = p;
    one.count = 1;
    list.count = 0;
    spin.angle = 0;
    spawn_from_pattern(&list, &one, BASE_W, BASE_H, 600, 300, &spin);
    check(list.count == 1, "count 1 spawns exactly one bullet");
}

static void test_spear_spin(void)
{
    printf("spear (spinning):\n");
    Pattern p = { 0 };
    p.name = "t";
    p.origin = ORIGIN_POINT;
    p.point = (Vector2){ 600, 300 };
    p.aim = AIM_SPEAR;
    p.count = 4;
    p.length = 100;
    p.speed = 300;
    p.has_angle = true;
    p.angle = 0;
    p.spin = 0.5f;
    p.radius = 6;
    p.color = YELLOW;
    p = pattern_apply_defaults(p);

    BulletList list;
    list.count = 0;
    SpinState spin = { 0 };

    spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);
    float first = heading(&list.items[0]);
    check(near(first, 0.0f, 1e-3f), "first shot points at .angle");
    check(near(spin.angle, 0.5f, 1e-4f), "spin state advanced once");

    list.count = 0;
    spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);
    check(near(heading(&list.items[0]), 0.5f, 1e-3f), "second shot rotated by .spin");
}

static void test_formation(void)
{
    printf("formation:\n");
    Pattern p = { 0 };
    p.name = "t";
    p.origin = ORIGIN_PLAYER;
    p.aim = AIM_FORMATION;
    p.segments = 6;
    p.count = 2;
    p.spread = 20.0f * PI / 180.0f;
    p.length = 140;
    p.speed = 200;
    p.radius = 6;
    p.color = YELLOW;
    p = pattern_apply_defaults(p);

    BulletList list;
    list.count = 0;
    SpinState spin = { 0 };
    spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);

    check(list.count == p.segments * p.count, "segments * count bullets spawned");

    /* the first bullet of each vertex sits at radius .length from the origin */
    bool on_ring = true;
    for (int s = 0; s < p.segments; s++) {
        const Bullet *b = &list.items[s * p.count];
        float r = hypotf(b->x - 600, b->y - 300);
        if (!near(r, p.length, 0.5f)) on_ring = false;
    }
    check(on_ring, "vertices sit on a ring of radius .length");

    /* each vertex fires at the player, so headings differ between vertices */
    bool aims_in = true;
    for (int s = 0; s < p.segments; s++) {
        const Bullet *b = &list.items[s * p.count];
        float to_player = atan2f(300 - b->y, 600 - b->x);
        if (!near(heading_gap(heading(b), to_player), 0.0f, 0.3f)) aims_in = false;
    }
    check(aims_in, "vertices fire toward the player");

    /* a pair from one vertex is fanned by .spread */
    const Bullet *a = &list.items[0], *b = &list.items[1];
    check(near(heading_gap(heading(a), heading(b)), p.spread, 1e-3f),
          "the two bullets at a vertex are .spread apart");

    /* degenerate segment counts must not crash or divide by zero */
    Pattern one = p;
    one.segments = 1;
    one.count = 1;
    list.count = 0;
    spin.angle = 0;
    spawn_from_pattern(&list, &one, BASE_W, BASE_H, 600, 300, &spin);
    check(list.count == 1, "segments 1 / count 1 spawns one bullet");
}

static void test_formation_static(void)
{
    printf("formation (pinned angle):\n");
    Pattern p = { 0 };
    p.name = "t";
    p.origin = ORIGIN_POINT;
    p.point = (Vector2){ 600, 300 };
    p.aim = AIM_FORMATION;
    p.segments = 3;
    p.count = 1;
    p.length = 100;
    p.speed = 250;
    p.has_angle = true;
    p.angle = 0;
    p.radius = 6;
    p.color = YELLOW;
    p = pattern_apply_defaults(p);

    BulletList list;
    list.count = 0;
    SpinState spin = { 0 };
    spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);

    check(list.count == 3, "3 bullets spawned");
    bool all_zero = true;
    for (int i = 0; i < list.count; i++)
        if (!near(heading(&list.items[i]), 0.0f, 1e-3f)) all_zero = false;
    check(all_zero, "pinned angle is used for every vertex");
}

static void test_all_patterns(void)
{
    printf("every built-in pattern:\n");
    int bad = 0, spawned = 0;
    for (int i = 0; i < PATTERN_COUNT; i++) {
        Pattern p = pattern_apply_defaults(PATTERNS[i]);
        BulletList list;
        list.count = 0;
        SpinState spin = { 0 };
        spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);
        spawned += list.count;
        if (list.count <= 0) {
            printf("  FAIL '%s' spawned nothing\n", p.name);
            bad++;
        }
        if (list.count > MAX_BULLETS) {
            printf("  FAIL '%s' overflowed the bullet list\n", p.name);
            bad++;
        }
        for (int j = 0; j < list.count; j++) {
            const Bullet *b = &list.items[j];
            if (!isfinite(b->x) || !isfinite(b->y) ||
                !isfinite(b->vx) || !isfinite(b->vy)) {
                printf("  FAIL '%s' produced a non-finite bullet\n", p.name);
                bad++;
                break;
            }
        }
    }
    check(bad == 0, "all patterns spawn finite, in-range bullets");
    printf("       (%d patterns, %d bullets total)\n", PATTERN_COUNT, spawned);
}

/* Sprite ids are per-name: same name gives the same id, "" gives -1 (circle). */
static void test_sprite_ids(void)
{
    printf("bullet sprites:\n");
    Pattern p = pattern_apply_defaults((Pattern){ 0 });
    p.aim = AIM_PLAYER;
    p.count = 2;
    p.speed = 300;
    snprintf(p.sprite, SPRITE_NAME_MAX, "bullet_orb");

    BulletList a, b;
    a.count = 0;
    b.count = 0;
    SpinState spin = { 0 };
    spawn_from_pattern(&a, &p, BASE_W, BASE_H, 600, 300, &spin);
    spin.angle = 0.0f;
    spawn_from_pattern(&b, &p, BASE_W, BASE_H, 600, 300, &spin);

    check(a.count == 2 && b.count == 2, "a sprite pattern still spawns its bullets");
    check(a.items[0].sprite >= 0, "the sprite name resolves to an id");
    check(a.items[0].sprite == a.items[1].sprite, "both bullets share one sprite id");
    check(a.items[0].sprite == b.items[0].sprite, "the same name resolves to the same id");
    check(near(a.items[0].sprite_size, p.radius * 2.0f, 0.01f), "sprite size defaults to the diameter");
    check(a.items[0].sprite_rot == SPRITE_ROT_NONE, "sprites default to upright");

    p.sprite[0] = '\0';
    a.count = 0;
    spawn_from_pattern(&a, &p, BASE_W, BASE_H, 600, 300, &spin);
    check(a.items[0].sprite == -1, "no sprite means the circle fallback");
}

int main(void)
{
    printf("shape tests\n===========\n");
    test_spear();
    test_spear_spin();
    test_formation();
    test_formation_static();
    test_all_patterns();
    test_sprite_ids();

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILED" : "all good", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}