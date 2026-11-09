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
#include "player_stats.h"
#include "patterns_io.h"
#include "pattern_script.h"
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

/* gravity: a shell fired level must speed up downward and leave the bottom */
static void test_gravity(void)
{
    printf("gravity shells:\n");
    Pattern p = pattern_apply_defaults((Pattern){ 0 });
    p.origin = ORIGIN_TOP;
    p.aim = AIM_ANGLE;
    p.has_angle = true;
    p.angle = 0.0f;          /* fired level: only gravity can move it down */
    p.count = 1;
    p.speed = 100;
    p.gravity = 400;

    BulletList list;
    list.count = 0;
    SpinState spin = { 0 };
    spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);
    check(list.count == 1, "a gravity shot still spawns");
    check(near(list.items[0].vy, 0.0f, 1e-3f), "fired level, so it starts level");

    for (int i = 0; i < 10; i++) update_bullets(&list, 0.1f, BASE_W, BASE_H);
    check(list.count == 1 && list.items[0].vy > 100.0f, "gravity keeps speeding it downward");

    int steps = 0;
    while (list.count > 0 && steps < 200) {
        update_bullets(&list, 0.1f, BASE_W, BASE_H);
        steps++;
    }
    check(list.count == 0, "a shell that falls past the bottom is culled");
}

/* alternating walls: same shot, opposite side, every other shot */
static void test_alternate(void)
{
    printf("alternating wall:\n");
    Pattern p = pattern_apply_defaults((Pattern){ 0 });
    p.aim = AIM_ALTERNATE;
    p.count = 5;
    p.speed = 200;

    BulletList a, b;
    a.count = 0;
    b.count = 0;
    SpinState spin = { 0 };
    spawn_from_pattern(&a, &p, BASE_W, BASE_H, 600, 300, &spin);
    spawn_from_pattern(&b, &p, BASE_W, BASE_H, 600, 300, &spin);

    check(a.count == 5 && b.count == 5, "both shots fire the full wall");
    check(spin.shot == 2, "the shot counter advances per shot");
    check(a.items[0].x < BASE_W / 2.0f && b.items[0].x > BASE_W / 2.0f,
          "the wall alternates sides");
}

/* lua scripts: resources/scripts/<name>.lua decides the bullets */
static void test_script(void)
{
    printf("lua scripts:\n");
    Pattern p = pattern_apply_defaults((Pattern){ 0 });
    p.aim = AIM_ANGLE;
    p.count = 5;
    p.speed = 300;
    p.spread = 20.0f * (PI / 180.0f);
    p.gravity = 500;
    snprintf(p.script, SCRIPT_NAME_MAX, "cannon");

    BulletList list;
    list.count = 0;
    SpinState spin = { 0 };
    spawn_from_pattern(&list, &p, BASE_W, BASE_H, 600, 300, &spin);
    check(list.count == 5, "the cannon script produced its shells");
    check(list.items[0].gravity == 500.0f, "scripted bullets keep the pattern gravity");

    bool finite = true;
    for (int i = 0; i < list.count; i++)
        if (!isfinite(list.items[i].x) || !isfinite(list.items[i].y) ||
            !isfinite(list.items[i].vx) || !isfinite(list.items[i].vy)) finite = false;
    check(finite, "scripted bullets are finite");

    /* a bad script name must warn and spawn nothing, never crash */
    Pattern missing = p;
    snprintf(missing.script, SCRIPT_NAME_MAX, "no_such_script_here");
    BulletList none;
    none.count = 0;
    spawn_from_pattern(&none, &missing, BASE_W, BASE_H, 600, 300, &spin);
    check(none.count == 0, "a missing script spawns nothing");
}

/* walls must keep the same hole on every difficulty: more bullets would
   shrink it, so every wall-like aim fires more often instead */
static void test_wall_scaling(void)
{
    printf("difficulty scaling of walls:\n");
    const DiffScale hard = { 1.25f, 1.5f, 1.0f };
    const DiffScale easy = { 0.85f, 0.75f, 0.8f };
    Aim wall_aims[] = { AIM_WALL, AIM_ALTERNATE };
    const char *names[] = { "wall", "alternate wall" };
    for (int k = 0; k < 2; k++) {
        Pattern p = pattern_apply_defaults((Pattern){ 0 });
        p.aim = wall_aims[k];
        p.count = 14;
        p.delay = 0.9f;
        p.speed = 260;
        Pattern h = scale_pattern(&p, hard);
        Pattern e = scale_pattern(&p, easy);
        char what[96];
        snprintf(what, sizeof what, "%s keeps its bullet count on Hard", names[k]);
        check(h.count == 14, what);
        snprintf(what, sizeof what, "%s keeps its bullet count on Easy", names[k]);
        check(e.count == 14, what);
        snprintf(what, sizeof what, "%s fires more often on Hard instead", names[k]);
        check(h.delay < p.delay, what);
    }
}

static bool write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fputs(text, f);
    fclose(f);
    return true;
}

/* scripts: one that forgets spawn(), the error being reported once, and the
   sandbox. Writes (and removes) throwaway files in resources/scripts. */
static void test_script_safety(void)
{
    printf("script safety:\n");
    const char *good = "resources/scripts/__t_good.lua";
    const char *nospawn = "resources/scripts/__t_nospawn.lua";
    const char *sandbox = "resources/scripts/__t_sandbox.lua";
    bool wrote = write_text(good, "function spawn(c) return {{x=1,y=1,angle=0,speed=1}} end\n") &&
                 write_text(nospawn, "-- forgot to define spawn\nlocal x = 1\n") &&
                 write_text(sandbox,
                     "function spawn(c)\n"
                     "  local clean = io == nil and os == nil and package == nil\n"
                     "      and require == nil and dofile == nil and loadfile == nil\n"
                     "  if clean and math.floor(2.5) == 2 and string.rep('a', 2) == 'aa' then\n"
                     "    return {{x=1,y=1,angle=0,speed=1}}\n"
                     "  end\n"
                     "  return {}\n"
                     "end\n");
    check(wrote, "wrote the throwaway test scripts (run from the repo root)");

    ScriptReload();
    ShotSpec out[8];
    char err[256] = { 0 };
    Pattern p = pattern_apply_defaults((Pattern){ 0 });
    p.speed = 10;

    snprintf(p.script, SCRIPT_NAME_MAX, "__t_good");
    check(ScriptSpawn(&p, BASE_W, BASE_H, 0, 0, 0, 0, out, 8, err, sizeof err) == 1,
          "a script that defines spawn() runs");

    /* this one used to silently run __t_good's spawn() */
    snprintf(p.script, SCRIPT_NAME_MAX, "__t_nospawn");
    err[0] = '\0';
    int n = ScriptSpawn(&p, BASE_W, BASE_H, 0, 0, 0, 0, out, 8, err, sizeof err);
    check(n < 0, "a script without spawn() is an error, not the previous script's spawn()");
    check(strstr(err, "spawn") != NULL, "the error says spawn() is missing");

    err[0] = '\0';
    n = ScriptSpawn(&p, BASE_W, BASE_H, 0, 0, 1, 0, out, 8, err, sizeof err);
    check(n < 0 && err[0] == '\0', "the same error is not repeated on the next shot");

    ScriptReload();
    n = ScriptSpawn(&p, BASE_W, BASE_H, 0, 0, 2, 0, out, 8, err, sizeof err);
    check(n < 0 && err[0] != '\0', "after ScriptReload() the error is reported again");

    snprintf(p.script, SCRIPT_NAME_MAX, "__t_sandbox");
    check(ScriptSpawn(&p, BASE_W, BASE_H, 0, 0, 0, 0, out, 8, err, sizeof err) == 1,
          "scripts cannot reach io, os, require, dofile or loadfile");

    ScriptReload();
    remove(good);
    remove(nospawn);
    remove(sandbox);
}

/* warn: the default, parsing, a save/load round trip, and a reload that must
   survive a bad file */
static PatternSet warn_set;

static void test_warn_and_reload(void)
{
    printf("warning before a pattern / reload:\n");
    Pattern a = pattern_apply_defaults((Pattern){ 0 });
    a.name = "A";
    a.origin = ORIGIN_POINT;
    a.point = (Vector2){ 600, 300 };
    check(pattern_warn_seconds(&a) == DEFAULT_WARN, "a fixed-point pattern warns by default");

    Pattern b = a;
    b.name = "B";
    b.warn = 2.5f;
    b.warn_radius = 90;
    check(pattern_warn_seconds(&b) == 2.5f, "an explicit warn is used");

    Pattern c = a;
    c.name = "C";
    c.warn = -1.0f;
    check(pattern_warn_seconds(&c) == 0.0f, "warn off removes the warning");

    Pattern d = pattern_apply_defaults((Pattern){ 0 });
    d.name = "D";
    d.origin = ORIGIN_EDGE;
    check(pattern_warn_seconds(&d) == 0.0f, "other patterns do not warn by default");
    d.warn = 1.5f;
    check(pattern_warn_seconds(&d) == 1.5f, "any pattern can ask for a warning");

    const char *tmp = "__shape_test_warn.tmp";
    Pattern list[4] = { a, b, c, d };
    check(PatternsSave(tmp, list, 4), "patterns with warn save");
    char err[256] = { 0 };
    int n = PatternsLoad(tmp, &warn_set, err, sizeof err);
    check(n == 4, "they load back");
    if (n == 4) {
        const Pattern *r = warn_set.items;
        check(pattern_warn_seconds(&r[0]) == DEFAULT_WARN, "default warn survives a save/load");
        check(pattern_warn_seconds(&r[1]) == 2.5f && near(r[1].warn_radius, 90, 1e-3f),
              "explicit warn and warn_radius survive a save/load");
        check(pattern_warn_seconds(&r[2]) == 0.0f, "warn off survives a save/load");
        check(pattern_warn_seconds(&r[3]) == 1.5f, "warn on a non-point pattern survives");
    }

    /* reload: a bad file must leave the live table alone */
    check(PatternsLoadRuntime(tmp), "a good file loads into the runtime table");
    int count = 0;
    const Pattern *live = PatternsGet(&count);
    check(count == 4 && strcmp(live[0].name, "A") == 0, "the runtime table has the new patterns and names");

    const char *bad = "__shape_test_bad.tmp";
    write_text(bad, "pattern x\n  not_a_key 1\nend\n");
    check(!PatternsLoadRuntime(bad), "a file with a typo is rejected");
    int count2 = 0;
    live = PatternsGet(&count2);
    check(count2 == 4 && strcmp(live[1].name, "B") == 0,
          "...and the patterns that were loaded stay in use");

    remove(tmp);
    remove(bad);
    PatternsReload();   /* put the real table back for anything that runs later */
}

/* player stats: defaults, a round trip, and a bad value reported */
static void test_player_stats(void)
{
    printf("player stats:\n");
    PlayerStats d = PlayerStatsDefaults();
    check(d.max_hp > 0 && d.speed > 0 && d.size > 0 && d.invuln >= 0, "defaults are sane");

    PlayerStats want;
    want.max_hp = 42;
    want.speed = 640.0f;
    want.size = 14.0f;
    want.invuln = 2.5f;
    const char *tmp = "tools/_player_test.txt";
    check(PlayerStatsSave(tmp, &want), "stats save");

    PlayerStats got;
    char err[256] = { 0 };
    check(PlayerStatsLoad(tmp, &got, err, sizeof err), "stats load");
    check(got.max_hp == 42 && near(got.speed, 640.0f, 0.01f) &&
          near(got.size, 14.0f, 0.01f) && near(got.invuln, 2.5f, 0.01f),
          "values survive the round trip");
    remove(tmp);

    PlayerStats missing;
    char missing_err[256] = { 0 };
    check(PlayerStatsLoad("tools/_no_such_player.txt", &missing, missing_err, sizeof missing_err),
          "a missing file is not an error");
    check(missing.max_hp == d.max_hp, "a missing file falls back to defaults");

    /* an out-of-range value must be reported, not silently used */
    FILE *f = fopen("tools/_bad_player.txt", "wb");
    if (f) {
        fprintf(f, "hp 0\n");
        fclose(f);
        PlayerStats bad;
        char bad_err[256] = { 0 };
        check(!PlayerStatsLoad("tools/_bad_player.txt", &bad, bad_err, sizeof bad_err),
              "hp 0 is rejected");
        check(strstr(bad_err, "hp") != NULL, "the error names the offending key");
        remove("tools/_bad_player.txt");
    }
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
    test_gravity();
    test_alternate();
    test_script();
    test_wall_scaling();
    test_script_safety();
    test_warn_and_reload();
    test_player_stats();

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILED" : "all good", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}