#include "raylib.h"
#include "bullet.h"
#include "patterns.h"
#include "patterns_io.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------------- start-up error reporting ---------------- */
/* raylib logs a fatal message (e.g. "Failed to initialize graphics device")
   and then exits. In a console-less build that message goes nowhere and the
   game just vanishes, so show it in a dialog instead.
   <windows.h> cannot be included here: it declares Rectangle() and
   CloseWindow(), which collide with raylib's names.
   The GLFW joystick shims that keep raylib from mistaking a DirectInput
   failure for a graphics failure live in platform_compat.c. */
#if defined(_WIN32)
__declspec(dllimport) int __stdcall MessageBoxA(void *hwnd, const char *text,
                                                const char *caption, unsigned int type);
#define MB_OK        0x00000000u
#define MB_ICONERROR 0x00000010u
#endif

static void show_fatal(int level, const char *text, va_list args)
{
    /* A custom trace callback replaces raylib's own output, so keep printing
       the normal log (visible in a CONSOLE=1 build) and only add the dialog
       for fatal messages. */
    va_list copy;
    va_copy(copy, args);
    vprintf(text, copy);
    printf("\n");
    fflush(stdout);
    va_end(copy);

    if (level != LOG_FATAL) return;

    char msg[1024];
    va_copy(copy, args);
    vsnprintf(msg, sizeof msg, text, copy);
    va_end(copy);

#if defined(_WIN32)
    MessageBoxA(NULL, msg, "Bullet Hell - failed to start", MB_OK | MB_ICONERROR);
#else
    fprintf(stderr, "FATAL: %s\n", msg);
#endif
}

/* ---------------- difficulty ---------------- */
/* Normal is the original tuning.
     player: player move speed (px/s)   speed: bullet speed multiplier
     count:  more/fewer bullets          damage: bullet damage multiplier */
typedef struct {
    const char *name;
    float player;
    DiffScale scale;
    Color color;
} Difficulty;

static const Difficulty DIFFS[] = {
    { "Easy",   500, { 0.85f, 0.75f, 0.8f }, {  90, 220, 120, 255 } },
    { "Normal", 500, { 1.00f, 1.00f, 1.0f }, { 255, 255, 255, 255 } },
    { "Hard",   600, { 1.25f, 1.50f, 1.0f }, { 230,  70,  70, 255 } },
};
#define DIFF_COUNT 3

/* ---------------- game state ---------------- */
typedef enum { STATE_MENU, STATE_PLAY } GameState;

static const char *MENU_ITEMS[] = { "Start", "Difficulty", "Quit" };
#define MENU_COUNT 3

static int width = 1200, height = 600;

static const float BALL_SIZE = 10;
static const int MAX_HP = 100;
static const float INVULN_TIME = 1.0f;

static float speed = 500;
static float pos_x, pos_y;
static int hp;
static float invuln;           /* seconds of i-frames left after a hit */

static BulletList bullets;     /* static: too big for the stack */
static Pattern active[MAX_PATTERNS];
static int pattern_index, shots_left;
static float shot_timer;
static SpinState spin_state;

/* pattern list actually used: resources/patterns.txt if present, else the
   built-in table from patterns.c (see patterns_io.c) */
static const Pattern *patterns = NULL;
static int pattern_count = 0;

static GameState state = STATE_MENU;
static int menu_sel = 0;
static int diff_index = 1;     /* Normal */
static bool quit_requested = false;

static Texture2D player_tex;
static bool have_tex = false;
static float tex_scale = 1.0f;

/* ---------------- helpers ---------------- */
static void draw_centered(const char *text, int y, int size, Color color)
{
    DrawText(text, (width - MeasureText(text, size)) / 2, y, size, color);
}

static void load_player_texture(void)
{
    /* like the python version: relative to the executable, so it works from
       any working directory. Falls back to a few other spots. */
    const char *app = GetApplicationDirectory();
    const char *candidates[] = {
        TextFormat("%s../resources/player.png", app),
        TextFormat("%sresources/player.png", app),
        TextFormat("%splayer.png", app),
        "../resources/player.png",
        "resources/player.png",
        "player.png",
    };
    for (int i = 0; i < 6; i++) {
        if (FileExists(candidates[i])) {
            player_tex = LoadTexture(candidates[i]);
            if (player_tex.id != 0) {
                have_tex = true;
                SetTextureFilter(player_tex, TEXTURE_FILTER_POINT);  /* crisp pixels */
                tex_scale = (BALL_SIZE * 2) / (float)player_tex.width;
                return;
            }
        }
    }
    TraceLog(LOG_WARNING, "player.png not found - drawing a circle instead");
}


static void draw_player(float x, float y, Color tint)
{
    if (have_tex) {
        float size = player_tex.width * tex_scale;
        DrawTextureEx(player_tex, (Vector2){ x - size / 2, y - size / 2 },
                      0.0f, tex_scale, tint);
    } else {
        DrawCircleV((Vector2){ x, y }, BALL_SIZE, tint);
    }
}

static void reset_run(void)
{
    const Difficulty *d = &DIFFS[diff_index];
    speed = d->player;
    for (int i = 0; i < pattern_count; i++)
        active[i] = scale_pattern(&patterns[i], d->scale);

    hp = MAX_HP;
    invuln = 0.0f;
    bullets.count = 0;
    pattern_index = 0;
    shots_left = active[0].repeat;
    shot_timer = 0.0f;
    spin_state.angle = 0.0f;
    pos_x = width / 2;
    pos_y = height / 2;
}

static void draw_health_bar(void)
{
    const int bar_w = 220, bar_h = 18, x = 10, y = 38;
    float frac = (float)hp / MAX_HP;
    DrawRectangle(x, y, bar_w, bar_h, (Color){ 40, 40, 40, 255 });
    DrawRectangle(x, y, (int)(bar_w * frac), bar_h,
                  frac > 0.3f ? (Color){ 60, 200, 90, 255 } : (Color){ 220, 70, 70, 255 });
    DrawRectangleLines(x, y, bar_w, bar_h, WHITE);
    DrawText(TextFormat("HP %d/%d", hp > 0 ? hp : 0, MAX_HP), x + bar_w + 8, y + 2, 16, WHITE);
}

/* circle-circle collision against every bullet; returns damage taken */
static int player_hit(void)
{
    if (invuln > 0) return 0;
    int taken = 0;
    for (int i = 0; i < bullets.count; i++) {
        Bullet *b = &bullets.items[i];
        if (b->dead) continue;
        float dx = b->x - pos_x, dy = b->y - pos_y;
        float rr = b->radius + BALL_SIZE;
        if (dx * dx + dy * dy <= rr * rr) {
            taken += b->damage;
            b->dead = true;
        }
    }
    if (taken) invuln = INVULN_TIME;
    return taken;
}

/* ---------------- menu ---------------- */
static Rectangle menu_item_rect(int i)
{
    float w = 300, h = 44;
    return (Rectangle){ (width - w) / 2, height / 2.0f - 20 + i * 56, w, h };
}

static void change_diff(int step)
{
    diff_index = (diff_index + step + DIFF_COUNT) % DIFF_COUNT;
}

static GameState run_menu(void)
{
    if (IsKeyPressed(KEY_DOWN)) menu_sel = (menu_sel + 1) % MENU_COUNT;
    if (IsKeyPressed(KEY_UP))   menu_sel = (menu_sel + MENU_COUNT - 1) % MENU_COUNT;

    if (strcmp(MENU_ITEMS[menu_sel], "Difficulty") == 0) {
        if (IsKeyPressed(KEY_RIGHT)) change_diff(1);
        if (IsKeyPressed(KEY_LEFT))  change_diff(-1);
    }

    Vector2 mouse = GetMousePosition();
    Vector2 delta = GetMouseDelta();
    if (delta.x != 0 || delta.y != 0) {
        for (int i = 0; i < MENU_COUNT; i++)
            if (CheckCollisionPointRec(mouse, menu_item_rect(i))) menu_sel = i;
    }
    bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                   CheckCollisionPointRec(mouse, menu_item_rect(menu_sel));

    if (IsKeyPressed(KEY_ENTER) || clicked) {
        const char *item = MENU_ITEMS[menu_sel];
        if (strcmp(item, "Start") == 0) {
            reset_run();
            return STATE_PLAY;
        }
        if (strcmp(item, "Difficulty") == 0) change_diff(1);
        else quit_requested = true;
    }
    return STATE_MENU;
}

static void draw_menu(void)
{
    draw_centered("BULLET HELL", height / 2 - 150, 60, WHITE);
    draw_centered("Arrow keys to move", height / 2 - 70, 20, GRAY);
    for (int i = 0; i < MENU_COUNT; i++) {
        Rectangle r = menu_item_rect(i);
        bool sel = (i == menu_sel);
        DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height,
                           sel ? YELLOW : GRAY);
        const char *label = MENU_ITEMS[i];
        Color color = sel ? YELLOW : WHITE;
        if (strcmp(label, "Difficulty") == 0) {
            label = TextFormat("< %s >", DIFFS[diff_index].name);
            color = DIFFS[diff_index].color;
        }
        int tw = MeasureText(label, 24);
        DrawText(label, (int)(r.x + (r.width - tw) / 2), (int)r.y + 10, 24, color);
    }
    draw_centered("Up/Down + Enter, Left/Right changes difficulty  |  ESC: menu in game  |  F5: reload patterns",
                  height - 40, 16, GRAY);
}

/* ---------------- gameplay ---------------- */
static void update_play(float dt)
{
    if (IsKeyPressed(KEY_ESCAPE)) {
        state = STATE_MENU;
        menu_sel = 0;
        return;
    }

    if (hp > 0) {
        /* player movement */
        float dx = 0, dy = 0;
        if (IsKeyDown(KEY_RIGHT)) dx += 1.00;
        if (IsKeyDown(KEY_LEFT))  dx -= 1.00;
        if (IsKeyDown(KEY_UP))    dy -= 1.00;
        if (IsKeyDown(KEY_DOWN))  dy += 1.00;
        if (dx != 0 || dy != 0) {
            float l = hypotf(dx, dy);
            dx /= l;
            dy /= l;
        }
        pos_x += dx * speed * dt;
        pos_y += dy * speed * dt;
        /* clamp against the live window size (it can be resized any time) */
        pos_x = fminf(fmaxf(pos_x, BALL_SIZE), width  - BALL_SIZE);
        pos_y = fminf(fmaxf(pos_y, BALL_SIZE), height - BALL_SIZE);

        invuln = fmaxf(0.0f, invuln - dt);

        /* pattern timer */
        shot_timer -= dt;
        if (shot_timer <= 0) {
            const Pattern *pat = &active[pattern_index];
            spawn_from_pattern(&bullets, pat, width, height, pos_x, pos_y, &spin_state);
            shot_timer = pat->delay;
            if (pat->repeat > 0) {
                shots_left--;
                if (shots_left <= 0) {
                    pattern_index = (pattern_index + 1) % pattern_count;
                    shots_left = active[pattern_index].repeat;
                    spin_state.angle = 0.0f;
                }
            }
        }

        update_bullets(&bullets, dt, width, height);
        hp -= player_hit();
    } else {
        /* dead: keep bullets moving so the arena drains, wait for restart */
        update_bullets(&bullets, dt, width, height);
        if (IsKeyPressed(KEY_R)) reset_run();
    }
}

static void draw_play(void)
{
    /* player sprite, flashing while invulnerable */
    if (hp > 0) {
        if (invuln <= 0 || ((int)(invuln * 20)) % 2 == 0)
            draw_player(pos_x, pos_y, WHITE);
        else
            draw_player(pos_x, pos_y, (Color){ 255, 255, 255, 100 });
    }

    draw_bullets(&bullets);

    if (hp > 0) {
        DrawText(active[pattern_index].name, 10, 10, 20, WHITE);
        const char *dn = DIFFS[diff_index].name;
        DrawText(dn, width - MeasureText(dn, 20) - 10, 10, 20, DIFFS[diff_index].color);
        draw_health_bar();
    } else {
        draw_centered("GAME OVER", height / 2 - 20, 40, RED);
        draw_centered("R: restart   |   ESC: menu", height / 2 + 30, 20, WHITE);
    }
}

int main(void)
{
    SetTraceLogCallback(show_fatal);         /* report start-up failures */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);   /* must be set before InitWindow */
    InitWindow(width, height, "Bullet Hell");
    if (!IsWindowReady()) {
#if defined(_WIN32)
        MessageBoxA(NULL, "Could not create the game window.", "Bullet Hell", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);                    /* ESC goes back to the menu */
    SetTargetFPS(60);

    /* patterns can be edited by hand in resources/patterns.txt
       (or with tools/pattern_editor.exe); the built-in table is the fallback */
    patterns = PatternsGet(&pattern_count);
    if (pattern_count <= 0) pattern_count = 1;

    load_player_texture();
    reset_run();

    while (!WindowShouldClose() && !quit_requested) {
        float dt = GetFrameTime();

        /* window can be resized at any time: track the live size */
        width = GetScreenWidth();
        height = GetScreenHeight();

        /* reload resources/patterns.txt without restarting the game */
        if (IsKeyPressed(KEY_F5)) {
            patterns = PatternsGet(&pattern_count);
            if (pattern_count <= 0) pattern_count = 1;
            reset_run();          /* clears bullets, so sprite ids are safe to drop */
            reload_bullet_sprites();
            state = STATE_MENU;
        }

        if (state == STATE_MENU) state = run_menu();
        else                     update_play(dt);

        BeginDrawing();
        ClearBackground(BLACK);
        if (state == STATE_MENU) draw_menu();
        else                     draw_play();
        EndDrawing();
    }

    if (have_tex) UnloadTexture(player_tex);
    CloseWindow();
    return 0;
}