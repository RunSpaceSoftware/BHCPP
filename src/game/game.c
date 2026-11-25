#include "game.h"

#include "audio.h"
#include "console.h"
#include "engine.h"
#include "input.h"
#include "pattern_script.h"
#include "patterns_io.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------------- difficulty ---------------- */
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

static Game *g_bound = NULL;

static Vector2 game_mouse(const Game *g)
{
    Vector2 m = GetMousePosition();
    float scale = (g && g->mouse_scale > 0.0f) ? g->mouse_scale : 1.0f;
    Vector2 origin = g ? g->mouse_origin : (Vector2){ 0, 0 };
    return (Vector2){ (m.x - origin.x) / scale, (m.y - origin.y) / scale };
}

static void draw_centered(const Game *g, const char *text, int y, int size, Color color)
{
    DrawText(text, (g->width - MeasureText(text, size)) / 2, y, size, color);
}

static void set_status(Game *g, bool ok, const char *text)
{
    snprintf(g->status_text, sizeof g->status_text, "%s", text ? text : "");
    g->status_ok = ok;
    g->status_timer = 5.0f;
}

/* ---------------- data loading ---------------- */
static void load_player_stats(Game *g)
{
    g->stats = PlayerStatsDefaults();
    char path[4096];
    if (!EngineResolveResource("player.txt", path, sizeof path)) return;
    char err[256] = { 0 };
    PlayerStats loaded;
    if (PlayerStatsLoad(path, &loaded, err, sizeof err)) {
        g->stats = loaded;
        EngineLogInfo("GAME: loaded player stats from %s", path);
    } else {
        EngineLogWarn("GAME: %s: %s (using defaults)", path, err);
    }
}

static void load_player_texture(Game *g)
{
    if (g->have_tex) {
        UnloadTexture(g->player_tex);
        g->have_tex = false;
    }
    char path[4096];
    if (!EngineResolveResource("player.png", path, sizeof path)) {
        EngineLogWarn("GAME: player.png not found - drawing a circle instead");
        return;
    }
    g->player_tex = LoadTexture(path);
    if (g->player_tex.id != 0) {
        g->have_tex = true;
        SetTextureFilter(g->player_tex, TEXTURE_FILTER_POINT);
    }
}

static int diff_get(void) { return g_bound ? g_bound->diff_index : 1; }

static void game_rebuild_active(Game *g);
static void begin_pattern(Game *g, int index);

static void diff_set(int v)
{
    if (!g_bound || v < 0 || v >= DIFF_COUNT) return;
    g_bound->diff_index = v;
    g_bound->speed = g_bound->stats.speed * (DIFFS[v].player / PlayerStatsDefaults().speed);
    if (g_bound->active_count <= 0) return;
    int keep = g_bound->pattern_index;
    game_rebuild_active(g_bound);
    if (keep >= g_bound->active_count) keep = 0;
    begin_pattern(g_bound, keep);
}

static const char *diff_text(int v) { return (v >= 0 && v < DIFF_COUNT) ? DIFFS[v].name : "?"; }

static void load_pause_menu(Game *g)
{
    PauseMenuDefaults(&g->pause_menu);
    char path[4096];
    if (!EngineResolveResource("pause.txt", path, sizeof path)) return;
    char err[256] = { 0 };
    if (PauseMenuLoad(path, &g->pause_menu, err, sizeof err)) {
        EngineLogInfo("GAME: %d pause items from %s", g->pause_menu.count, path);
    } else {
        EngineLogWarn("GAME: %s: %s (using the built-in menu)", path, err);
    }
}

static void fallback_levels(Game *g)
{
    const char *names[MAX_PATTERNS];
    int count = g->table_count;
    if (count > MAX_PATTERNS) count = MAX_PATTERNS;
    for (int i = 0; i < count; i++) names[i] = g->patterns[i].name;
    if (!LevelsDefault(&g->levels, names, count)) {
        g->has_levels = false;
        g->levels.count = 0;
        return;
    }
    g->has_levels = true;
    g->level_index = 0;
}

void GameLoadLevels(Game *g, const char *path)
{
    if (!g) return;
    char resolved[4096];
    const char *use = path;
    if (!use || !*use) {
        if (EngineResolveResource("levels.txt", resolved, sizeof resolved)) use = resolved;
    }
    if (use && *use) {
        char err[256] = { 0 };
        if (LevelsLoad(use, &g->levels, err, sizeof err)) {
            g->has_levels = g->levels.count > 0;
            g->level_index = 0;
            EngineLogInfo("GAME: %d levels from %s", g->levels.count, use);
            return;
        }
        EngineLogWarn("GAME: %s: %s (using all patterns)", use, err);
    }
    fallback_levels(g);
}

static int find_pattern(const Game *g, const char *name)
{
    for (int i = 0; i < g->table_count; i++)
        if (strcmp(g->patterns[i].name, name) == 0) return i;
    return -1;
}

static void game_rebuild_active(Game *g)
{
    const Difficulty *d = &DIFFS[g->diff_index];
    g->active_count = 0;

    if (g->has_levels && g->level_index >= 0 && g->level_index < g->levels.count) {
        const Level *level = &g->levels.items[g->level_index];
        for (int i = 0; i < level->pattern_count && g->active_count < MAX_PATTERNS; i++) {
            int index = find_pattern(g, level->patterns[i]);
            if (index < 0) {
                EngineLogWarn("GAME: level '%s' names unknown pattern '%s'",
                              level->name, level->patterns[i]);
                continue;
            }
            g->active[g->active_count++] = scale_pattern(&g->patterns[index], d->scale);
        }
    }

    if (g->active_count == 0) {
        int count = g->table_count;
        if (count > MAX_PATTERNS) count = MAX_PATTERNS;
        for (int i = 0; i < count; i++)
            g->active[g->active_count++] = scale_pattern(&g->patterns[i], d->scale);
    }
}

static void begin_pattern(Game *g, int index)
{
    if (g->active_count <= 0) return;
    index = ((index % g->active_count) + g->active_count) % g->active_count;
    g->pattern_index = index;
    g->shots_left = g->active[index].repeat;
    g->spin_state.angle = 0.0f;
    g->spin_state.shot = 0;

    float warn = pattern_warn_seconds(&g->active[index]);
    g->warn_pending = warn > 0.0f;
    if (g->warn_pending && g->shot_timer < warn) g->shot_timer = warn;
    else if (g->shot_timer < 0.0f) g->shot_timer = 0.0f;
}

static void begin_level(Game *g, int index)
{
    if (!g->has_levels || g->levels.count <= 0) {
        game_rebuild_active(g);
        begin_pattern(g, 0);
        return;
    }
    index = ((index % g->levels.count) + g->levels.count) % g->levels.count;
    g->level_index = index;
    game_rebuild_active(g);
    begin_pattern(g, 0);

    char text[160];
    snprintf(text, sizeof text, "LEVEL %d  -  %s", index + 1, g->levels.items[index].name);
    set_status(g, true, text);
}

void GameReset(Game *g)
{
    if (!g) return;
    const Difficulty *d = &DIFFS[g->diff_index];
    g->speed = g->stats.speed * (d->player / PlayerStatsDefaults().speed);
    g->hp = g->stats.max_hp;
    g->invuln = 0.0f;
    g->bullets.count = 0;
    g->shot_timer = 0.0f;
    g->level_index = 0;
    game_rebuild_active(g);
    begin_pattern(g, 0);

    g->pos_x = g->width * 0.5f;
    g->pos_y = g->height * 0.5f;
    if (g->scene) {
        int spawn = SceneFindTag(g->scene, "player_spawn");
        if (spawn >= 0) {
            Vector2 p = SceneWorldPosition(g->scene, spawn);
            g->pos_x = p.x;
            g->pos_y = p.y;
        }
    }
    g->pos_x = fminf(fmaxf(g->pos_x, g->stats.size), g->width - g->stats.size);
    g->pos_y = fminf(fmaxf(g->pos_y, g->stats.size), g->height - g->stats.size);
    g->screen = GAME_SCREEN_PLAY;
    g->pause_run = true;
    g->warn_pending = false;
}

void GameToMenu(Game *g)
{
    if (!g) return;
    g->screen = GAME_SCREEN_MENU;
    g->menu_sel = 0;
    g->pause_sel = 0;
    g->pause_run = false;
    g->bullets.count = 0;
}

/* ---------------- drawing ---------------- */
static void draw_player(const Game *g, float x, float y, Color tint)
{
    if (g->have_tex) {
        float scale = (g->stats.size * 2.0f) / (float)g->player_tex.width;
        float size = g->player_tex.width * scale;
        DrawTextureEx(g->player_tex, (Vector2){ x - size / 2, y - size / 2 },
                      0.0f, scale, tint);
    } else {
        DrawCircleV((Vector2){ x, y }, g->stats.size, tint);
    }
}

static void draw_health_bar(const Game *g)
{
    const int bar_w = 220, bar_h = 18, x = 10, y = 38;
    int max_hp = g->stats.max_hp > 0 ? g->stats.max_hp : 1;
    float frac = (float)g->hp / max_hp;
    DrawRectangle(x, y, bar_w, bar_h, (Color){ 40, 40, 40, 255 });
    DrawRectangle(x, y, (int)(bar_w * frac), bar_h,
                  frac > 0.3f ? (Color){ 60, 200, 90, 255 } : (Color){ 220, 70, 70, 255 });
    DrawRectangleLines(x, y, bar_w, bar_h, WHITE);
    DrawText(TextFormat("HP %d/%d", g->hp > 0 ? g->hp : 0, max_hp),
             x + bar_w + 8, y + 2, 16, WHITE);
}

/* ---------------- collision ---------------- */
static int player_hit(Game *g)
{
    if (g->invuln > 0.0f) return 0;
    for (int i = 0; i < g->bullets.count; i++) {
        Bullet *b = &g->bullets.items[i];
        if (b->dead) continue;
        float dx = b->x - g->pos_x, dy = b->y - g->pos_y;
        float rr = b->radius + g->stats.size;
        if (dx * dx + dy * dy <= rr * rr) {
            b->dead = true;
            g->invuln = g->stats.invuln;
            return b->damage;
        }
    }
    return 0;
}

/* ---------------- menu ---------------- */
static int menu_column_width(const MenuItem *items, int count, int size)
{
    int w = 380;
    for (int i = 0; i < count; i++) {
        int tw = MeasureText(PauseMenuItemText(&items[i]), size) + 60;
        if (tw > w) w = tw;
    }
    return w;
}

static Rectangle menu_row_rect(const Game *g, int count, int index, int bw_max)
{
    const int row = 56, bh = 44;
    float block_top = g->height / 2.0f - (count * row) / 2.0f;
    return (Rectangle){ (g->width - bw_max) / 2.0f, block_top + index * row,
                        (float)bw_max, (float)bh };
}

static MenuAction menu_nav(Game *g, const MenuItem *items, int count, int *sel, bool use_mouse)
{
    if (count <= 0) return MENU_ACTION_NONE;
    if (*sel < 0) *sel = 0;
    if (*sel >= count) *sel = count - 1;
    int bw_max = menu_column_width(items, count, 24);

    if (IsKeyPressed(KEY_DOWN)) *sel = (*sel + 1) % count;
    if (IsKeyPressed(KEY_UP))   *sel = (*sel + count - 1) % count;

    if (items[*sel].value[0]) {
        int step = IsKeyPressed(KEY_RIGHT) - IsKeyPressed(KEY_LEFT);
        if (step) PauseMenuValueSet((MenuItem *)&items[*sel],
                                    PauseMenuStep(PauseMenuValueGet(&items[*sel]),
                                                  items[*sel].min, items[*sel].max, step));
    }

    if (use_mouse) {
        Vector2 mouse = game_mouse(g);
        Vector2 delta = GetMouseDelta();
        if (delta.x != 0.0f || delta.y != 0.0f) {
            for (int i = 0; i < count; i++)
                if (CheckCollisionPointRec(mouse, menu_row_rect(g, count, i, bw_max))) *sel = i;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int i = 0; i < count; i++) {
                if (CheckCollisionPointRec(mouse, menu_row_rect(g, count, i, bw_max))) {
                    *sel = i;
                    return items[i].action;
                }
            }
        }
    }

    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) return items[*sel].action;
    return MENU_ACTION_NONE;
}

static void draw_menu_block(const Game *g, const char *title, int title_size, const char *hint,
                            const MenuItem *items, int count, int sel)
{
    const int size = 24;
    int bw_max = menu_column_width(items, count, size);
    Rectangle first = menu_row_rect(g, count, 0, bw_max);

    if (title) draw_centered(g, title, (int)first.y - title_size - 24, title_size, WHITE);
    if (hint && *hint) draw_centered(g, hint, g->height - 60, 16, GRAY);

    for (int i = 0; i < count; i++) {
        Rectangle r = menu_row_rect(g, count, i, bw_max);
        bool sel_row = (i == sel);
        DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, sel_row ? YELLOW : GRAY);
        const char *text = PauseMenuItemText(&items[i]);
        int tw = MeasureText(text, size);
        DrawText(text, (int)(r.x + (bw_max - tw) / 2), (int)r.y + 10, size,
                 sel_row ? YELLOW : WHITE);
    }
}

static int build_main_items(MenuItem *items)
{
    memset(items, 0, sizeof(MenuItem) * PAUSE_MAX_ITEMS);
    snprintf(items[0].label, sizeof items[0].label, "Start");
    items[0].action = MENU_ACTION_START;
    snprintf(items[1].label, sizeof items[1].label, "Difficulty");
    snprintf(items[1].value, sizeof items[1].value, "difficulty");
    items[1].min = 0;
    items[1].max = DIFF_COUNT - 1;
    snprintf(items[2].label, sizeof items[2].label, "Quit");
    items[2].action = MENU_ACTION_QUIT;
    return 3;
}

static void draw_menu(Game *g)
{
    MenuItem items[PAUSE_MAX_ITEMS];
    int count = build_main_items(items);
    draw_menu_block(g, "BULLET HELL", 60,
                    "Arrow keys to move  |  Up/Down + Enter, Left/Right changes difficulty  |  F5: reload",
                    items, count, g->menu_sel);
    if (g->status_timer > 0.0f)
        draw_centered(g, g->status_text, g->height - 90, 18,
                      g->status_ok ? (Color){ 90, 220, 120, 255 } : (Color){ 230, 70, 70, 255 });
}

static void open_pause(Game *g)
{
    g->pause_run = (g->hp > 0);
    g->pause_sel = 0;
    g->screen = GAME_SCREEN_PAUSE;
}

static int build_pause_items(const Game *g, MenuItem *items)
{
    int count = 0;
    if (g->pause_run) {
        MenuItem resume;
        memset(&resume, 0, sizeof resume);
        snprintf(resume.label, sizeof resume.label, "Resume");
        resume.action = MENU_ACTION_RESUME;
        items[count++] = resume;
    }
    for (int i = 0; i < g->pause_menu.count && count < PAUSE_MAX_ITEMS; i++)
        items[count++] = g->pause_menu.items[i];
    return count;
}

static void draw_pause(Game *g)
{
    MenuItem items[PAUSE_MAX_ITEMS];
    int count = build_pause_items(g, items);
    draw_menu_block(g, g->pause_menu.title, 40, g->pause_menu.hint, items, count, g->pause_sel);
}

static void run_menu(Game *g)
{
    MenuItem items[PAUSE_MAX_ITEMS];
    int count = build_main_items(items);
    MenuAction action = menu_nav(g, items, count, &g->menu_sel, true);
    if (action == MENU_ACTION_START) {
        GameReset(g);
        g->menu_sel = 0;
    } else if (action == MENU_ACTION_QUIT) {
        g->quit_requested = true;
    }
}

static void run_pause(Game *g)
{
    MenuItem items[PAUSE_MAX_ITEMS];
    int count = build_pause_items(g, items);
    if (IsKeyPressed(KEY_ESCAPE)) { g->screen = GAME_SCREEN_PLAY; return; }

    switch (menu_nav(g, items, count, &g->pause_sel, true)) {
    case MENU_ACTION_RESUME:  g->screen = GAME_SCREEN_PLAY; break;
    case MENU_ACTION_RESTART: GameReset(g); break;
    case MENU_ACTION_START:   GameReset(g); break;
    case MENU_ACTION_MENU:    GameToMenu(g); break;
    case MENU_ACTION_QUIT:    g->quit_requested = true; break;
    default: break;
    }
}

/* ---------------- gameplay ---------------- */
static void next_pattern(Game *g)
{
    int next = g->pattern_index + 1;
    if (next >= g->active_count) {
        if (g->has_levels && g->levels.count > 1) {
            begin_level(g, g->level_index + 1);
            return;
        }
        next = 0;
    }
    begin_pattern(g, next);
}

static void update_play(Game *g, float dt)
{
    if (IsKeyPressed(KEY_ESCAPE)) { open_pause(g); return; }

    if (g->hp > 0) {
        Vector2 move = InputMoveVector("game_left", "game_right", "game_up", "game_down");
        g->pos_x += move.x * g->speed * dt;
        g->pos_y += move.y * g->speed * dt;
        g->pos_x = fminf(fmaxf(g->pos_x, g->stats.size), g->width - g->stats.size);
        g->pos_y = fminf(fmaxf(g->pos_y, g->stats.size), g->height - g->stats.size);
        g->invuln = fmaxf(0.0f, g->invuln - dt);

        g->shot_timer -= dt;
        if (g->shot_timer <= 0.0f) {
            const Pattern *pat = &g->active[g->pattern_index];
            g->warn_pending = false;
            spawn_from_pattern(&g->bullets, pat, g->width, g->height,
                               g->pos_x, g->pos_y, &g->spin_state);
            AudioPlay(pat->sound);
            g->shot_timer = pat->delay > 0.02f ? pat->delay : 0.02f;
            if (pat->repeat > 0) {
                g->shots_left--;
                if (g->shots_left <= 0) next_pattern(g);
            }
        }

        update_bullets(&g->bullets, dt, g->width, g->height);
        int damage = player_hit(g);
        if (damage > 0) g->hp -= damage;
    } else {
        update_bullets(&g->bullets, dt, g->width, g->height);
        if (IsKeyPressed(KEY_R)) GameReset(g);
    }
}

static void draw_warning(const Game *g)
{
    if (g->active_count <= 0) return;
    const Pattern *pat = &g->active[g->pattern_index];
    float warn = pattern_warn_seconds(pat);
    if (!g->warn_pending || g->shot_timer >= warn) return;

    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 16.0f);
    Color col = pat->color;
    if (pat->origin == ORIGIN_POINT) {
        float sx = (float)g->width / BASE_W, sy = (float)g->height / BASE_H;
        Vector2 c = { pat->point.x * sx, pat->point.y * sy };
        float r = pat->warn_radius * (sx < sy ? sx : sy);
        DrawCircleV(c, r, Fade(col, 0.10f + 0.15f * pulse));
        DrawRing(c, r - 4, r, 0, 360, 64, Fade(col, 0.5f + 0.5f * pulse));
        draw_centered(g, "DANGER ZONE - MOVE AWAY", 14, 24, Fade(col, 0.6f + 0.4f * pulse));
    } else {
        draw_centered(g, "INCOMING", 14, 24, Fade(col, 0.6f + 0.4f * pulse));
    }
}

static void draw_play(Game *g)
{
    if (g->hp > 0) {
        if (g->invuln <= 0 || ((int)(g->invuln * 20)) % 2 == 0)
            draw_player(g, g->pos_x, g->pos_y, WHITE);
        else
            draw_player(g, g->pos_x, g->pos_y, (Color){ 255, 255, 255, 100 });
    }

    if (g->hp > 0) draw_warning(g);
    draw_bullets(&g->bullets);

    if (g->hp > 0) {
        DrawText(GameCurrentPattern(g), 10, 10, 20, WHITE);
        const char *dn = DIFFS[g->diff_index].name;
        DrawText(dn, g->width - MeasureText(dn, 20) - 10, 10, 20, DIFFS[g->diff_index].color);
        if (g->has_levels && g->level_index < g->levels.count) {
            const char *ln = g->levels.items[g->level_index].name;
            DrawText(ln, 10, g->height - 28, 18, (Color){ 150, 160, 175, 255 });
        }
        draw_health_bar(g);
        if (g->status_timer > 0.0f)
            draw_centered(g, g->status_text, 72, 18, (Color){ 120, 220, 150, 255 });
    } else {
        draw_centered(g, "GAME OVER", g->height / 2 - 20, 40, RED);
        draw_centered(g, "ESC: pause   |   R: restart", g->height / 2 + 30, 20, WHITE);
    }
}

/* ---------------- public API ---------------- */
bool GameInit(Game *g, int width, int height)
{
    if (!g) return false;
    memset(g, 0, sizeof *g);
    g->width = width > 0 ? width : BASE_W;
    g->height = height > 0 ? height : BASE_H;
    g->diff_index = 1;
    g->mouse_scale = 1.0f;
    g_bound = g;

    InputBind("game_left", KEY_LEFT);
    InputBind("game_left", KEY_A);
    InputBind("game_right", KEY_RIGHT);
    InputBind("game_right", KEY_D);
    InputBind("game_up", KEY_UP);
    InputBind("game_up", KEY_W);
    InputBind("game_down", KEY_DOWN);
    InputBind("game_down", KEY_S);

    g->patterns = PatternsGet(&g->table_count);
    if (g->table_count <= 0) return false;

    load_player_stats(g);
    load_player_texture(g);
    PauseMenuBindValue("difficulty", 0, DIFF_COUNT - 1, diff_get, diff_set, diff_text);
    load_pause_menu(g);
    GameLoadLevels(g, NULL);

    GameReset(g);
    g->screen = GAME_SCREEN_MENU;
    g->pause_run = false;
    g->quit_requested = false;
    return true;
}

void GameShutdown(Game *g)
{
    if (!g) return;
    if (g->have_tex) {
        UnloadTexture(g->player_tex);
        g->have_tex = false;
    }
    if (g_bound == g) g_bound = NULL;
}

void GameSetSize(Game *g, int width, int height)
{
    if (!g) return;
    g->width = width > 0 ? width : BASE_W;
    g->height = height > 0 ? height : BASE_H;
    g->pos_x = fminf(fmaxf(g->pos_x, g->stats.size), g->width - g->stats.size);
    g->pos_y = fminf(fmaxf(g->pos_y, g->stats.size), g->height - g->stats.size);
}

void GameSetScene(Game *g, const Scene *scene) { if (g) g->scene = scene; }

void GameSetMouseTransform(Game *g, Vector2 origin, float scale)
{
    if (!g) return;
    g->mouse_origin = origin;
    g->mouse_scale = scale > 0.0f ? scale : 1.0f;
}

void GameUpdate(Game *g, float dt)
{
    if (!g) return;
    if (g->status_timer > 0.0f) g->status_timer -= dt;
    if (g->screen == GAME_SCREEN_MENU) run_menu(g);
    else if (g->screen == GAME_SCREEN_PLAY) update_play(g, dt);
    else run_pause(g);
}

void GameDraw(Game *g)
{
    if (!g) return;
    if (g->screen == GAME_SCREEN_MENU) {
        draw_menu(g);
        return;
    }
    draw_play(g);
    if (g->screen == GAME_SCREEN_PAUSE) {
        DrawRectangle(0, 0, g->width, g->height, (Color){ 0, 0, 0, 190 });
        draw_pause(g);
    }
}

bool GameReload(Game *g)
{
    if (!g) return false;
    g->bullets.count = 0;
    bool loaded = PatternsReload();
    ScriptReload();
    g->patterns = PatternsGet(&g->table_count);
    if (g->table_count <= 0) g->table_count = 1;
    load_player_stats(g);
    load_pause_menu(g);
    load_player_texture(g);
    GameLoadLevels(g, NULL);
    reload_bullet_sprites();
    AudioReload();
    GameReset(g);
    g->screen = GAME_SCREEN_MENU;
    g->pause_run = false;

    if (loaded) {
        char text[160];
        snprintf(text, sizeof text, "Reloaded %d patterns, scripts, sprites and stats", g->table_count);
        set_status(g, true, text);
    } else if (PatternsPath()[0]) {
        set_status(g, false, "patterns.txt has an error - kept the previous patterns (see console)");
    } else {
        set_status(g, false, "No patterns.txt found - using the built-in patterns");
    }
    return loaded;
}

bool GameWantsQuit(const Game *g) { return g && g->quit_requested; }

const char *GameCurrentPattern(const Game *g)
{
    if (!g || g->active_count <= 0 || g->pattern_index < 0 || g->pattern_index >= g->active_count)
        return "";
    return g->active[g->pattern_index].name;
}
