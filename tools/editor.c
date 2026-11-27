#include "raylib.h"

#include "assets.h"
#include "audio.h"
#include "bullet.h"
#include "console.h"
#include "engine.h"
#include "game.h"
#include "level.h"
#include "pattern_script.h"
#include "patterns.h"
#include "patterns_io.h"
#include "player_stats.h"
#include "scene.h"
#include "ui.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 1440
#define WIN_H 900
#define MENU_H 30.0f
#define TOOLBAR_H 46.0f
#define STATUS_H 26.0f
#define UNDO_MAX 32
#define CONSOLE_HISTORY 20

typedef struct {
    Rectangle menu, toolbar, left, right, center, viewport, dock, status;
    Rectangle left_split, right_split, bottom_split;
} Layout;

typedef enum {
    TOOL_SELECT = 0,
    TOOL_MOVE,
    TOOL_ROTATE,
    TOOL_SCALE
} EditorTool;

typedef enum {
    LEFT_HIERARCHY = 0,
    LEFT_ASSETS
} LeftTab;

typedef enum {
    DOCK_CONSOLE = 0,
    DOCK_PATTERNS,
    DOCK_LEVELS
} DockTab;

typedef enum {
    ACT_NONE = 0,
    ACT_NEW_SCENE, ACT_SAVE_SCENE, ACT_RELOAD_ASSETS, ACT_QUIT,
    ACT_UNDO, ACT_REDO, ACT_DUPLICATE, ACT_DELETE,
    ACT_PLAY, ACT_PAUSE, ACT_STEP, ACT_STOP, ACT_RELOAD_GAME,
    ACT_TOGGLE_GRID, ACT_TOGGLE_SNAP, ACT_RESET_LAYOUT, ACT_FRAME_ARENA,
    ACT_ABOUT, ACT_CONSOLE_HELP
} EditorAction;

typedef struct {
    const char *label;
    const char *shortcut;
    EditorAction action;
} MenuEntry;

typedef struct {
    const char *label;
    MenuEntry items[10];
    int count;
    float x;
    float width;
} MenuDef;

typedef struct {
    Scene scene;
    char scene_path[4096];
    bool scene_dirty;
    int selected;
    bool expanded[SCENE_MAX_ENTITIES];

    Scene undo[UNDO_MAX];
    Scene redo[UNDO_MAX];
    int undo_count, redo_count;
    Scene pending;
    bool pending_valid;
    bool drag_undo_pushed;

    float left_w, right_w, bottom_h;
    int left_tab;
    int bottom_tab;
    int tool;
    bool show_grid;
    bool snap;
    float snap_size;

    Vector2 cam_center;
    float cam_zoom;
    bool view_ready;

    int menu_open;
    MenuDef menus[5];
    bool show_about;

    int split_drag;
    int scene_drag;
    Vector2 drag_mouse;
    Vector2 drag_pos;
    float drag_rot;
    Vector2 drag_scale;

    bool playing, paused, step_once;
    Game *game;
    RenderTexture2D play_rt;
    bool play_rt_ready;

    PatternSet patterns;
    int pattern_sel;
    char pattern_search[64];
    bool pattern_search_editing;
    bool pattern_dirty;
    char pattern_path[4096];
    int preview_pattern;
    BulletList preview;
    float preview_timer;
    SpinState preview_spin;

    LevelSet levels;
    int level_sel;
    bool levels_dirty;
    char levels_path[4096];

    AssetNameList textures;
    AssetNameList scripts;
    AssetNameList data;
    AssetNameList scene_files;
    int asset_tab;
    char selected_asset[ASSET_NAME_MAX];

    char console_input[256];
    bool console_editing;
    bool console_follow;
    float console_scroll;
    char console_history[CONSOLE_HISTORY][256];
    int console_history_count;
    int console_history_pos;

    bool edit_name, edit_tag, edit_sprite, edit_scene_name;
    bool edit_pattern_name, edit_level_name;
    bool text_focus;

    float hier_scroll;
    float asset_scroll;
    float inspector_scroll;
    float pattern_list_scroll;
    float pattern_prop_scroll;
    float level_list_scroll;
    float level_prop_scroll;

    char status[256];
    bool status_ok;
    float status_timer;

    Layout layout;
    int win_w, win_h;
    bool running;
} Editor;

static Editor *g_editor = NULL;

/* ---------------- small helpers ---------------- */
static void say(Editor *ed, bool ok, const char *fmt, ...)
{
    if (!ed) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(ed->status, sizeof ed->status, fmt, args);
    va_end(args);
    ed->status_ok = ok;
    ed->status_timer = 6.0f;
    ConsolePrint(ok ? CONSOLE_OUTPUT : CONSOLE_ERROR, "%s", ed->status);
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float snap_value(float v, float step)
{
    if (step <= 0.0f) return v;
    return roundf(v / step) * step;
}

static void ensure_parent_dir(const char *path)
{
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", path);
    char *slash = strrchr(tmp, '/');
    char *bslash = strrchr(tmp, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (!slash) return;
    *slash = '\0';
    if (tmp[0]) MakeDirectory(tmp);
}

static UIIcon entity_icon(const Entity *e)
{
    if (!e) return UI_ICON_ENTITY;
    if (strcmp(e->tag, "arena") == 0) return UI_ICON_GRID;
    if (strcmp(e->tag, "player_spawn") == 0) return UI_ICON_PLAY;
    if (strcmp(e->tag, "enemy") == 0) return UI_ICON_WARNING;
    if (strcmp(e->tag, "camera") == 0) return UI_ICON_CAMERA;
    return UI_ICON_ENTITY;
}

static Color entity_color(const Entity *e)
{
    if (!e) return (Color){ 150, 160, 175, 255 };
    if (strcmp(e->tag, "arena") == 0) return (Color){ 90, 140, 220, 255 };
    if (strcmp(e->tag, "player_spawn") == 0) return (Color){ 90, 220, 150, 255 };
    if (strcmp(e->tag, "enemy") == 0) return (Color){ 230, 100, 110, 255 };
    return (Color){ 180, 190, 205, 255 };
}

/* ---------------- layout ---------------- */
static Layout layout_compute(int w, int h, float left_w, float right_w, float bottom_h)
{
    Layout L;
    memset(&L, 0, sizeof L);
    float top = MENU_H + TOOLBAR_H;
    float bottom = (float)h - STATUS_H;

    if (left_w < 180.0f) left_w = 180.0f;
    if (left_w > 380.0f) left_w = 380.0f;
    if (right_w < 240.0f) right_w = 240.0f;
    if (right_w > 460.0f) right_w = 460.0f;
    float max_bottom = bottom - top - 220.0f;
    if (max_bottom < 140.0f) max_bottom = 140.0f;
    if (bottom_h < 140.0f) bottom_h = 140.0f;
    if (bottom_h > max_bottom) bottom_h = max_bottom;

    L.menu = (Rectangle){ 0, 0, (float)w, MENU_H };
    L.toolbar = (Rectangle){ 0, MENU_H, (float)w, TOOLBAR_H };
    L.status = (Rectangle){ 0, bottom, (float)w, STATUS_H };

    L.left = (Rectangle){ 8, top + 8, left_w, bottom - top - 16 };
    L.right = (Rectangle){ w - right_w - 8, top + 8, right_w, bottom - top - 16 };
    float cx = L.left.x + L.left.width + 8.0f;
    float cw = L.right.x - cx - 8.0f;
    if (cw < 240.0f) cw = 240.0f;
    L.center = (Rectangle){ cx, top + 8, cw, bottom - top - 16 };

    float vh = L.center.height - bottom_h - 8.0f;
    if (vh < 160.0f) vh = 160.0f;
    L.viewport = (Rectangle){ L.center.x, L.center.y, L.center.width, vh };
    L.dock = (Rectangle){ L.center.x, L.viewport.y + L.viewport.height + 8.0f,
                          L.center.width, bottom_h };

    L.left_split = (Rectangle){ L.left.x + L.left.width + 2.0f, L.left.y, 4.0f, L.left.height };
    L.right_split = (Rectangle){ L.right.x - 6.0f, L.right.y, 4.0f, L.right.height };
    L.bottom_split = (Rectangle){ L.center.x, L.dock.y - 5.0f, L.center.width, 4.0f };
    return L;
}

static void layout_apply(Editor *ed)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    if (w < 800) w = 800;
    if (h < 560) h = 560;
    ed->win_w = w;
    ed->win_h = h;
    ed->layout = layout_compute(w, h, ed->left_w, ed->right_w, ed->bottom_h);
    if (!ed->view_ready) {
        ed->cam_center = (Vector2){ BASE_W * 0.5f, BASE_H * 0.5f };
        float zx = (ed->layout.viewport.width - 50.0f) / BASE_W;
        float zy = (ed->layout.viewport.height - 50.0f) / BASE_H;
        ed->cam_zoom = zx < zy ? zx : zy;
        if (ed->cam_zoom < 0.1f) ed->cam_zoom = 0.1f;
        ed->view_ready = true;
    }
}

static int layout_check(void)
{
    int problems = 0;
    int sizes[][2] = { { 1440, 900 }, { 1100, 700 }, { 1920, 1080 } };
    for (int i = 0; i < 3; i++) {
        int w = sizes[i][0], h = sizes[i][1];
        Layout L = layout_compute(w, h, 270.0f, 320.0f, 270.0f);
        if (L.left.width <= 0 || L.right.width <= 0 || L.center.width <= 0) {
            printf("FAIL %dx%d: non-positive dock width\n", w, h); problems++;
        }
        if (L.viewport.height < 80 || L.dock.height < 80) {
            printf("FAIL %dx%d: viewport/dock too short (%.0f/%.0f)\n",
                   w, h, L.viewport.height, L.dock.height); problems++;
        }
        if (L.left.x + L.left.width >= L.center.x ||
            L.center.x + L.center.width > L.right.x) {
            printf("FAIL %dx%d: docks overlap\n", w, h); problems++;
        }
        if (L.dock.y + L.dock.height > L.status.y) {
            printf("FAIL %dx%d: dock overlaps status bar\n", w, h); problems++;
        }
        printf("%dx%d: left %.0f, viewport %.0fx%.0f, dock %.0f, right %.0f\n",
               w, h, L.left.width, L.viewport.width, L.viewport.height,
               L.dock.height, L.right.width);
    }
    if (problems == 0) printf("OK: editor layout fits at 1100x700 and up\n");
    return problems == 0 ? 0 : 1;
}

/* ---------------- undo ---------------- */
static void undo_push_scene(Editor *ed, const Scene *snap)
{
    if (ed->undo_count >= UNDO_MAX) {
        memmove(&ed->undo[0], &ed->undo[1], sizeof(Scene) * (UNDO_MAX - 1));
        ed->undo_count = UNDO_MAX - 1;
    }
    ed->undo[ed->undo_count++] = *snap;
    ed->redo_count = 0;
    ed->scene_dirty = true;
}

static void undo_capture(Editor *ed)
{
    undo_push_scene(ed, &ed->scene);
    ed->drag_undo_pushed = true;
    ed->pending_valid = false;
}

static void undo_apply(Editor *ed)
{
    if (ed->undo_count <= 0) return;
    if (ed->redo_count >= UNDO_MAX) {
        memmove(&ed->redo[0], &ed->redo[1], sizeof(Scene) * (UNDO_MAX - 1));
        ed->redo_count = UNDO_MAX - 1;
    }
    ed->redo[ed->redo_count++] = ed->scene;
    ed->scene = ed->undo[--ed->undo_count];
    if (ed->selected >= ed->scene.count) ed->selected = ed->scene.count - 1;
    ed->scene_dirty = true;
    ed->pending_valid = false;
    ed->drag_undo_pushed = false;
}

static void redo_apply(Editor *ed)
{
    if (ed->redo_count <= 0) return;
    if (ed->undo_count >= UNDO_MAX) {
        memmove(&ed->undo[0], &ed->undo[1], sizeof(Scene) * (UNDO_MAX - 1));
        ed->undo_count = UNDO_MAX - 1;
    }
    ed->undo[ed->undo_count++] = ed->scene;
    ed->scene = ed->redo[--ed->redo_count];
    if (ed->selected >= ed->scene.count) ed->selected = ed->scene.count - 1;
    ed->scene_dirty = true;
    ed->pending_valid = false;
    ed->drag_undo_pushed = false;
}

static void edit_drag(Editor *ed)
{
    if (!ed->drag_undo_pushed) {
        if (ed->pending_valid) undo_push_scene(ed, &ed->pending);
        else undo_push_scene(ed, &ed->scene);
        ed->drag_undo_pushed = true;
    }
    ed->scene_dirty = true;
}

/* ---------------- scene file ---------------- */
static void scene_save(Editor *ed)
{
    if (!ed->scene_path[0]) {
        say(ed, false, "No scene path - nothing saved");
        return;
    }
    ensure_parent_dir(ed->scene_path);
    if (SceneSave(&ed->scene, ed->scene_path)) {
        ed->scene_dirty = false;
        say(ed, true, "Saved scene: %s", ed->scene_path);
    } else {
        say(ed, false, "Could not write %s", ed->scene_path);
    }
}

static void scene_load(Editor *ed, const char *path)
{
    if (!path || !*path) return;
    Scene tmp;
    char err[256] = { 0 };
    if (!SceneLoad(&tmp, path, err, sizeof err)) {
        say(ed, false, "Scene load failed: %s", err);
        return;
    }
    undo_capture(ed);
    ed->scene = tmp;
    snprintf(ed->scene_path, sizeof ed->scene_path, "%s", path);
    ed->selected = -1;
    ed->scene_dirty = false;
    ed->edit_name = ed->edit_tag = ed->edit_sprite = ed->edit_scene_name = false;
    say(ed, true, "Opened scene: %s", path);
}

static void scene_new(Editor *ed)
{
    undo_capture(ed);
    SceneDefault(&ed->scene);
    char user[4096];
    if (EngineUserDir(user, sizeof user))
        snprintf(ed->scene_path, sizeof ed->scene_path, "%.4079s/untitled.scene", user);
    else
        ed->scene_path[0] = '\0';
    ed->selected = -1;
    ed->scene_dirty = true;
    ed->edit_name = ed->edit_tag = ed->edit_sprite = ed->edit_scene_name = false;
    say(ed, true, "New scene created");
}

static void scene_init_path(Editor *ed)
{
    char path[4096];
    if (EngineResolveResource("scenes/main.scene", path, sizeof path)) {
        snprintf(ed->scene_path, sizeof ed->scene_path, "%s", path);
        if (FileExists(path)) {
            char err[256] = { 0 };
            if (!SceneLoad(&ed->scene, path, err, sizeof err))
                say(ed, false, "Could not open %s: %s", path, err);
            return;
        }
    } else {
        char user[4096];
        if (EngineUserDir(user, sizeof user))
            snprintf(ed->scene_path, sizeof ed->scene_path, "%.4084s/main.scene", user);
    }

    SceneDefault(&ed->scene);
    ed->scene_dirty = true;
    if (ed->scene_path[0]) {
        ensure_parent_dir(ed->scene_path);
        SceneSave(&ed->scene, ed->scene_path);
    }
}

/* ---------------- patterns ---------------- */
static void patterns_copy_builtin(Editor *ed)
{
    const Pattern *list = PatternsGet(&ed->patterns.count);
    for (int i = 0; i < ed->patterns.count; i++) {
        ed->patterns.items[i] = pattern_apply_defaults(list[i]);
        snprintf(ed->patterns.names[i], PATTERN_NAME_MAX, "%s", list[i].name);
        ed->patterns.items[i].name = ed->patterns.names[i];
    }
}

static void patterns_load_editor(Editor *ed)
{
    const char *runtime = PatternsPath();
    if (runtime && *runtime) snprintf(ed->pattern_path, sizeof ed->pattern_path, "%s", runtime);
    else snprintf(ed->pattern_path, sizeof ed->pattern_path, "resources/patterns.txt");

    char err[256] = { 0 };
    int n = PatternsLoad(ed->pattern_path, &ed->patterns, err, sizeof err);
    if (n > 0) {
        ed->pattern_dirty = false;
        ed->pattern_sel = 0;
        return;
    }
    patterns_copy_builtin(ed);
    ed->pattern_dirty = false;
    ed->pattern_sel = 0;
    if (err[0]) say(ed, false, "Could not read %s: %s", ed->pattern_path, err);
}

static bool patterns_save_editor(Editor *ed)
{
    ensure_parent_dir(ed->pattern_path);
    for (int i = 0; i < ed->patterns.count; i++)
        ed->patterns.items[i].name = ed->patterns.names[i];
    if (!PatternsSave(ed->pattern_path, ed->patterns.items, ed->patterns.count)) {
        say(ed, false, "Could not write %s", ed->pattern_path);
        return false;
    }
    ed->pattern_dirty = false;
    PatternsLoadRuntime(ed->pattern_path);
    say(ed, true, "Saved %d patterns to %s", ed->patterns.count, ed->pattern_path);
    return true;
}

/* ---------------- levels ---------------- */
static void levels_fallback(Editor *ed)
{
    const char *names[MAX_PATTERNS];
    int n = ed->patterns.count;
    if (n > MAX_PATTERNS) n = MAX_PATTERNS;
    for (int i = 0; i < n; i++) names[i] = ed->patterns.names[i];
    if (!LevelsDefault(&ed->levels, names, n)) {
        ed->levels.count = 1;
        snprintf(ed->levels.items[0].name, LEVEL_NAME_MAX, "Level 1");
        ed->levels.items[0].pattern_count = 0;
    }
    ed->level_sel = 0;
}

static void levels_load_editor(Editor *ed)
{
    char path[4096];
    if (EngineResolveResource("levels.txt", path, sizeof path))
        snprintf(ed->levels_path, sizeof ed->levels_path, "%s", path);
    else
        snprintf(ed->levels_path, sizeof ed->levels_path, "resources/levels.txt");

    if (FileExists(ed->levels_path)) {
        char err[256] = { 0 };
        if (LevelsLoad(ed->levels_path, &ed->levels, err, sizeof err)) {
            ed->levels_dirty = false;
            ed->level_sel = 0;
            return;
        }
        say(ed, false, "Levels: %s", err);
    }
    levels_fallback(ed);
    ed->levels_dirty = false;
}

static bool levels_save_editor(Editor *ed)
{
    ensure_parent_dir(ed->levels_path);
    if (!LevelsSave(ed->levels_path, &ed->levels)) {
        say(ed, false, "Could not write %s", ed->levels_path);
        return false;
    }
    ed->levels_dirty = false;
    say(ed, true, "Saved %d levels to %s", ed->levels.count, ed->levels_path);
    return true;
}

/* ---------------- assets ---------------- */
static void assets_scan_editor(Editor *ed)
{
    AssetsScan("", ".png", &ed->textures);
    AssetsScan("scripts", ".lua", &ed->scripts);
    AssetsScan("", ".txt", &ed->data);
    AssetsScan("scenes", ".scene", &ed->scene_files);
}

/* ---------------- play in editor ---------------- */
static Rectangle play_dest(const Editor *ed, Vector2 *origin, float *scale)
{
    Rectangle body = ed->layout.viewport;
    body.x += 1; body.y += 1; body.width -= 2; body.height -= 2;
    float z = body.width / BASE_W;
    float zy = body.height / BASE_H;
    if (zy < z) z = zy;
    if (z <= 0) z = 1;
    Rectangle dest = { body.x + (body.width - BASE_W * z) * 0.5f,
                       body.y + (body.height - BASE_H * z) * 0.5f,
                       BASE_W * z, BASE_H * z };
    if (origin) *origin = (Vector2){ dest.x, dest.y };
    if (scale) *scale = z;
    return dest;
}

static void play_stop(Editor *ed)
{
    if (ed->game) {
        GameShutdown(ed->game);
        free(ed->game);
        ed->game = NULL;
    }
    if (ed->play_rt_ready) {
        UnloadRenderTexture(ed->play_rt);
        ed->play_rt_ready = false;
    }
    ed->playing = false;
    ed->paused = false;
    ed->step_once = false;
}

static void play_start(Editor *ed)
{
    if (ed->playing) return;
    ed->game = (Game *)calloc(1, sizeof(Game));
    if (!ed->game) { say(ed, false, "Out of memory starting the game"); return; }
    if (!GameInit(ed->game, BASE_W, BASE_H)) {
        free(ed->game);
        ed->game = NULL;
        say(ed, false, "Game failed to start (see console)");
        return;
    }
    GameSetScene(ed->game, &ed->scene);
    GameLoadLevels(ed->game, ed->levels_path);
    GameReset(ed->game);

    ed->play_rt = LoadRenderTexture(BASE_W, BASE_H);
    ed->play_rt_ready = ed->play_rt.texture.id != 0;
    if (ed->play_rt_ready) SetTextureFilter(ed->play_rt.texture, TEXTURE_FILTER_BILINEAR);
    ed->playing = true;
    ed->paused = false;
    ed->step_once = false;
    say(ed, true, "Playing in editor - click the viewport to focus");
}

static void play_toggle_pause(Editor *ed)
{
    if (!ed->playing) return;
    ed->paused = !ed->paused;
    ed->step_once = false;
}

static void play_step(Editor *ed)
{
    if (!ed->playing) return;
    ed->paused = true;
    ed->step_once = true;
}

static void play_update(Editor *ed, float dt)
{
    if (!ed->playing || !ed->game) return;
    GameSetScene(ed->game, &ed->scene);
    Vector2 origin; float scale;
    (void)play_dest(ed, &origin, &scale);
    GameSetMouseTransform(ed->game, origin, scale);

    float step = 0.0f;
    if (ed->step_once) { step = 1.0f / 60.0f; ed->step_once = false; }
    if (!ed->paused || step > 0.0f) {
        float use = step > 0.0f ? step : dt;
        GameUpdate(ed->game, use);
        if (GameWantsQuit(ed->game)) {
            play_stop(ed);
            say(ed, true, "Game returned to the editor");
        }
    }
}

/* ---------------- preview ---------------- */
static void preview_reset(Editor *ed)
{
    ed->preview.count = 0;
    ed->preview_timer = 0.0f;
    ed->preview_spin.angle = 0.0f;
    ed->preview_spin.shot = 0;
    ed->preview_pattern = ed->pattern_sel;
}

static void preview_update(Editor *ed, float dt)
{
    if (ed->bottom_tab != DOCK_PATTERNS || ed->playing) return;
    if (ed->preview_pattern != ed->pattern_sel) preview_reset(ed);
    if (ed->patterns.count <= 0) return;

    ed->preview_timer -= dt;
    if (ed->preview_timer <= 0.0f) {
        Pattern p = ed->patterns.items[ed->pattern_sel];
        spawn_from_pattern(&ed->preview, &p, BASE_W, BASE_H, BASE_W * 0.5f, BASE_H * 0.5f,
                           &ed->preview_spin);
        ed->preview_timer = p.delay > 0.15f ? p.delay : 0.7f;
        if (ed->preview.count > MAX_BULLETS - 512) ed->preview.count = 0;
    }
    update_bullets(&ed->preview, dt, BASE_W, BASE_H);
}

/* ---------------- actions ---------------- */
static void duplicate_selected(Editor *ed)
{
    if (ed->selected < 0) return;
    undo_capture(ed);
    int at = SceneDuplicate(&ed->scene, ed->selected);
    if (at >= 0) { ed->selected = at; ed->scene_dirty = true; say(ed, true, "Duplicated entity"); }
}

static void delete_selected(Editor *ed)
{
    if (ed->selected < 0) return;
    undo_capture(ed);
    SceneRemove(&ed->scene, ed->selected);
    if (ed->selected >= ed->scene.count) ed->selected = ed->scene.count - 1;
    ed->scene_dirty = true;
    ed->edit_name = ed->edit_tag = ed->edit_sprite = false;
    say(ed, true, "Deleted entity");
}

static void reload_all(Editor *ed)
{
    if (ed->playing) play_stop(ed);
    AssetsReloadAll();
    ScriptReload();
    PatternsReload();
    patterns_load_editor(ed);
    levels_load_editor(ed);
    assets_scan_editor(ed);
    preview_reset(ed);
    say(ed, true, "Reloaded assets, scripts, patterns and levels");
}

static void menu_do(Editor *ed, EditorAction action)
{
    switch (action) {
    case ACT_NEW_SCENE: scene_new(ed); break;
    case ACT_SAVE_SCENE: scene_save(ed); break;
    case ACT_RELOAD_ASSETS: reload_all(ed); break;
    case ACT_QUIT: ed->running = false; break;
    case ACT_UNDO: undo_apply(ed); break;
    case ACT_REDO: redo_apply(ed); break;
    case ACT_DUPLICATE: duplicate_selected(ed); break;
    case ACT_DELETE: delete_selected(ed); break;
    case ACT_PLAY:
        if (!ed->playing) play_start(ed);
        else { ed->paused = false; }
        break;
    case ACT_PAUSE: play_toggle_pause(ed); break;
    case ACT_STEP: play_step(ed); break;
    case ACT_STOP: play_stop(ed); say(ed, true, "Stopped play mode"); break;
    case ACT_RELOAD_GAME:
        if (ed->playing && ed->game) { GameReload(ed->game); GameReset(ed->game); }
        else play_start(ed);
        break;
    case ACT_TOGGLE_GRID: ed->show_grid = !ed->show_grid; break;
    case ACT_TOGGLE_SNAP: ed->snap = !ed->snap; break;
    case ACT_RESET_LAYOUT: ed->left_w = 270; ed->right_w = 320; ed->bottom_h = 270; ed->view_ready = false; break;
    case ACT_FRAME_ARENA: ed->view_ready = false; layout_apply(ed); break;
    case ACT_ABOUT: ed->show_about = true; break;
    case ACT_CONSOLE_HELP: ed->bottom_tab = DOCK_CONSOLE; ConsoleExec("help"); break;
    default: break;
    }
}

static void menus_init(Editor *ed)
{
    ed->menus[0] = (MenuDef){ "File", {
        { "New Scene", "Ctrl+N", ACT_NEW_SCENE },
        { "Save Scene", "Ctrl+S", ACT_SAVE_SCENE },
        { "Reload Assets", "F5", ACT_RELOAD_ASSETS },
        { "Quit", "Alt+F4", ACT_QUIT }
    }, 4, 0, 0 };
    ed->menus[1] = (MenuDef){ "Edit", {
        { "Undo", "Ctrl+Z", ACT_UNDO },
        { "Redo", "Ctrl+Y", ACT_REDO },
        { "Duplicate Entity", "Ctrl+D", ACT_DUPLICATE },
        { "Delete Entity", "Del", ACT_DELETE }
    }, 4, 0, 0 };
    ed->menus[2] = (MenuDef){ "Game", {
        { "Play / Resume", "", ACT_PLAY },
        { "Pause", "", ACT_PAUSE },
        { "Step Frame", "", ACT_STEP },
        { "Stop", "", ACT_STOP },
        { "Reload Game", "", ACT_RELOAD_GAME }
    }, 5, 0, 0 };
    ed->menus[3] = (MenuDef){ "View", {
        { "Toggle Grid", "G", ACT_TOGGLE_GRID },
        { "Toggle Snap", "S", ACT_TOGGLE_SNAP },
        { "Frame Arena", "F", ACT_FRAME_ARENA },
        { "Reset Layout", "", ACT_RESET_LAYOUT }
    }, 4, 0, 0 };
    ed->menus[4] = (MenuDef){ "Help", {
        { "About BHCPP Engine", "", ACT_ABOUT },
        { "Console Help", "", ACT_CONSOLE_HELP }
    }, 2, 0, 0 };
}

static bool action_available(const Editor *ed, EditorAction action)
{
    switch (action) {
    case ACT_UNDO: return ed->undo_count > 0;
    case ACT_REDO: return ed->redo_count > 0;
    case ACT_DUPLICATE:
    case ACT_DELETE: return ed->selected >= 0;
    case ACT_PAUSE:
    case ACT_STEP:
    case ACT_STOP: return ed->playing;
    default: return true;
    }
}

/* ---------------- keyboard ---------------- */
static bool editing_text(const Editor *ed)
{
    return ed->edit_name || ed->edit_tag || ed->edit_sprite || ed->edit_scene_name ||
           ed->edit_pattern_name || ed->edit_level_name || ed->console_editing ||
           ed->pattern_search_editing;
}

static void editor_shortcuts(Editor *ed)
{
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl) {
        if (IsKeyPressed(KEY_S)) scene_save(ed);
        else if (IsKeyPressed(KEY_N)) scene_new(ed);
        else if (IsKeyPressed(KEY_Z)) undo_apply(ed);
        else if (IsKeyPressed(KEY_Y)) redo_apply(ed);
        else if (IsKeyPressed(KEY_D)) duplicate_selected(ed);
        return;
    }
    if (IsKeyPressed(KEY_F5)) reload_all(ed);
    if (ed->playing) return;
    if (editing_text(ed)) return;
    if (IsKeyPressed(KEY_DELETE)) delete_selected(ed);
    if (IsKeyPressed(KEY_G)) ed->show_grid = !ed->show_grid;
    if (IsKeyPressed(KEY_S)) ed->snap = !ed->snap;
    if (IsKeyPressed(KEY_F)) ed->view_ready = false;
    if (IsKeyPressed(KEY_ONE)) ed->tool = TOOL_SELECT;
    if (IsKeyPressed(KEY_TWO)) ed->tool = TOOL_MOVE;
    if (IsKeyPressed(KEY_THREE)) ed->tool = TOOL_ROTATE;
    if (IsKeyPressed(KEY_FOUR)) ed->tool = TOOL_SCALE;
}

/* ---------------- update ---------------- */
static void editor_update(Editor *ed, float dt)
{
    if (ed->status_timer > 0.0f) ed->status_timer -= dt;
    editor_shortcuts(ed);
    layout_apply(ed);
    preview_update(ed, dt);
    play_update(ed, dt);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !ed->playing) {
        ed->pending = ed->scene;
        ed->pending_valid = true;
        ed->drag_undo_pushed = false;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        ed->drag_undo_pushed = false;
        ed->pending_valid = false;
    }
}

/* ---------------- menu bar / toolbar / status ---------------- */
static void draw_menu_bar(Editor *ed)
{
    Layout *L = &ed->layout;
    UITheme *t = UIThemeGet();
    DrawRectangleGradientV(0, 0, (int)L->menu.width, (int)L->menu.height,
                           (Color){ 34, 39, 49, 255 }, (Color){ 24, 28, 35, 255 });
    DrawRectangle(0, (int)L->menu.height - 1, (int)L->menu.width, 1, t->border);

    Rectangle logo = { 10, 5, 20, 20 };
    DrawRectangleRounded(logo, 0.25f, 6, t->accent);
    UIDrawText("B", logo.x + 6.0f, logo.y + 2.0f, 16, (Color){ 10, 16, 24, 255 });
    UIDrawText("BHCPP", 37, 6, 15, t->text);
    UIDrawText("ENGINE", 91, 8, 11, t->accent);

    float x = 150.0f;
    for (int i = 0; i < 5; i++) {
        float w = UITextWidth(ed->menus[i].label, 14) + 22.0f;
        Rectangle r = { x, 3, w, 24 };
        ed->menus[i].x = x;
        ed->menus[i].width = w;
        if (UIFlatButton(ed->menus[i].label, r, ed->menus[i].label, ed->menu_open == i)) {
            ed->menu_open = (ed->menu_open == i) ? -1 : i;
            ed->show_about = false;
        }
        x += w + 2.0f;
    }

    const char *v = TextFormat("%s  v%s", ENGINE_NAME, EngineVersion());
    float vw = UITextWidth(v, 11);
    UIDrawText(v, L->menu.width - vw - 12.0f, 9, 11, t->text_disabled);
}

static void draw_dropdown(Editor *ed)
{
    if (ed->menu_open < 0 || ed->menu_open >= 5) return;
    MenuDef *m = &ed->menus[ed->menu_open];
    float w = 238.0f;
    float h = 8.0f + m->count * 28.0f;
    float x = m->x;
    float y = MENU_H + 2.0f;
    if (x + w > ed->win_w - 6) x = ed->win_w - w - 6;
    Rectangle box = { x, y, w, h };
    UITheme *t = UIThemeGet();
    UIShadow(box, 0.12f, 4);
    DrawRectangleRounded(box, 0.12f, 8, (Color){ 27, 31, 39, 252 });
    DrawRectangleRoundedLinesEx(box, 0.12f, 8, 1.0f, t->border_light);

    bool clicked_inside = false;
    for (int i = 0; i < m->count; i++) {
        MenuEntry *e = &m->items[i];
        Rectangle row = { box.x + 4, box.y + 4 + i * 28.0f, box.width - 8, 26 };
        bool enabled = action_available(ed, e->action);
        bool hover = enabled && UIMouseIn(row);
        bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (hover) DrawRectangleRounded(row, 0.2f, 6, t->selection);
        UIDrawText(e->label, row.x + 10, row.y + 6, 14, enabled ? t->text : t->text_disabled);
        if (e->shortcut && e->shortcut[0]) {
            float sw = UITextWidth(e->shortcut, 11);
            UIDrawText(e->shortcut, row.x + row.width - sw - 10, row.y + 8, 11, t->text_disabled);
        }
        if (clicked) { clicked_inside = true; menu_do(ed, e->action); ed->menu_open = -1; break; }
    }

    /* The same press that opened the menu is still down here. Ignore clicks
       on the menu bar, otherwise the dropdown closes on the opening click. */
    if (!clicked_inside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !UIMouseInRaw(box) && !UIMouseInRaw(ed->layout.menu))
        ed->menu_open = -1;
}

static void toolbar_sep(float x, float y, float h)
{
    DrawRectangle((int)x, (int)y, 1, (int)h, UIThemeGet()->border);
}

static void draw_toolbar(Editor *ed)
{
    Layout *L = &ed->layout;
    UITheme *t = UIThemeGet();
    DrawRectangle((int)L->toolbar.x, (int)L->toolbar.y, (int)L->toolbar.width,
                  (int)L->toolbar.height, (Color){ 27, 31, 38, 255 });
    DrawRectangle((int)L->toolbar.x, (int)(L->toolbar.y + L->toolbar.height - 1),
                  (int)L->toolbar.width, 1, t->border);

    float y = L->toolbar.y + 7.0f;
    float h = 32.0f;
    float x = 10.0f;

    if (UIIconButton("tb_play", (Rectangle){ x, y, h, h }, UI_ICON_PLAY,
                     ed->playing && !ed->paused, true, "Play (in editor)"))
        menu_do(ed, ACT_PLAY);
    x += h + 2;
    if (UIIconButton("tb_pause", (Rectangle){ x, y, h, h }, UI_ICON_PAUSE,
                     ed->playing && ed->paused, ed->playing, "Pause"))
        menu_do(ed, ACT_PAUSE);
    x += h + 2;
    if (UIIconButton("tb_step", (Rectangle){ x, y, h, h }, UI_ICON_STEP,
                     false, ed->playing, "Step one frame"))
        menu_do(ed, ACT_STEP);
    x += h + 2;
    if (UIIconButton("tb_stop", (Rectangle){ x, y, h, h }, UI_ICON_STOP,
                     false, ed->playing, "Stop"))
        menu_do(ed, ACT_STOP);
    x += h + 10;
    toolbar_sep(x, y + 2, h - 4);
    x += 10;

    struct { const char *id; UIIcon icon; int tool; const char *tip; } tools[] = {
        { "tb_select", UI_ICON_ENTITY, TOOL_SELECT, "Select (1)" },
        { "tb_move", UI_ICON_MOVE, TOOL_MOVE, "Move (2)" },
        { "tb_rotate", UI_ICON_ROTATE, TOOL_ROTATE, "Rotate (3)" },
        { "tb_scale", UI_ICON_SCALE, TOOL_SCALE, "Scale (4)" },
    };
    for (int i = 0; i < 4; i++) {
        if (UIIconButton(tools[i].id, (Rectangle){ x, y, h, h }, tools[i].icon,
                         ed->tool == tools[i].tool, true, tools[i].tip))
            ed->tool = tools[i].tool;
        x += h + 2;
    }
    x += 8;
    toolbar_sep(x, y + 2, h - 4);
    x += 10;

    if (UIIconButton("tb_grid", (Rectangle){ x, y, h, h }, UI_ICON_GRID,
                     ed->show_grid, true, "Grid (G)")) ed->show_grid = !ed->show_grid;
    x += h + 2;
    if (UIIconButton("tb_snap", (Rectangle){ x, y, h, h }, UI_ICON_SNAP,
                     ed->snap, true, "Snap (S)")) ed->snap = !ed->snap;
    x += h + 4;

    const char *snap_label = TextFormat("Snap %.0f", ed->snap ? ed->snap_size : 0.0f);
    if (UIButton("tb_snap_val", (Rectangle){ x, y + 2, 74, h - 4 }, snap_label, true)) {
        if (ed->snap_size < 5.0f) ed->snap_size = 5.0f;
        else if (ed->snap_size < 10.0f) ed->snap_size = 10.0f;
        else if (ed->snap_size < 25.0f) ed->snap_size = 25.0f;
        else if (ed->snap_size < 50.0f) ed->snap_size = 50.0f;
        else ed->snap_size = 1.0f;
        ed->snap = true;
    }
    x += 82;

    if (ed->playing) {
        const char *mode = ed->paused ? "PAUSED" : "PLAY";
        Color c = ed->paused ? t->warning : t->success;
        DrawRectangleRounded((Rectangle){ x, y + 6, 64, 20 }, 0.3f, 6, Fade(c, 0.16f));
        UIDrawText(mode, x + 12, y + 9, 12, c);
    }

    const char *title = ed->scene.name[0] ? ed->scene.name : "Untitled Scene";
    char line[256];
    snprintf(line, sizeof line, "%s%s", title, ed->scene_dirty ? " *" : "");
    float tw = UITextWidth(line, 13);
    UIDrawText(line, L->toolbar.width - tw - 14, y + 10, 13,
               ed->scene_dirty ? t->warning : t->text_dim);
}

static void draw_status(Editor *ed)
{
    Layout *L = &ed->layout;
    UITheme *t = UIThemeGet();
    DrawRectangle((int)L->status.x, (int)L->status.y, (int)L->status.width,
                  (int)L->status.height, (Color){ 22, 26, 32, 255 });
    DrawRectangle((int)L->status.x, (int)L->status.y, (int)L->status.width, 1, t->border);
    Color dot = ed->playing ? (ed->paused ? t->warning : t->success) : t->accent;
    DrawCircleV((Vector2){ 14, L->status.y + 13 }, 4, dot);
    const char *msg = (ed->status_timer > 0.0f && ed->status[0]) ? ed->status : "Ready";
    Color mc = ed->status_timer > 0.0f ? (ed->status_ok ? t->text : t->error) : t->text_dim;
    UIDrawText(msg, 24, L->status.y + 6, 12, mc);

    char mid[256];
    snprintf(mid, sizeof mid, "%.120s  |  %d entities  |  %s", ed->scene_path,
             ed->scene.count, ed->playing ? "PLAY MODE" : "Scene view");
    char mid_clip[256];
    UIClipText(mid, 11, L->status.width * 0.55f, mid_clip, sizeof mid_clip);
    float mw = UITextWidth(mid_clip, 11);
    UIDrawText(mid_clip, L->status.width * 0.36f - mw * 0.5f, L->status.y + 7, 11, t->text_disabled);

    const char *right = TextFormat("%d FPS   |   %.0f%%", GetFPS(), ed->cam_zoom * 100.0f);
    float rw = UITextWidth(right, 11);
    UIDrawText(right, L->status.width - rw - 12, L->status.y + 7, 11, t->text_disabled);
}

/* ---------------- shared panel helpers ---------------- */
static void select_entity(Editor *ed, int index)
{
    if (index < -1) index = -1;
    if (index >= ed->scene.count) index = ed->scene.count - 1;
    if (ed->selected != index) {
        ed->edit_name = ed->edit_tag = ed->edit_sprite = false;
        ed->inspector_scroll = 0;
    }
    ed->selected = index;
}

static bool prop_text(Editor *ed, const char *id, float x, float *y, float w,
                      const char *label, char *buf, int cap, bool *editing)
{
    UITheme *t = UIThemeGet();
    UIDrawText(label, x, *y + 6, 12, t->text_dim);
    Rectangle r = { x + 92, *y, w - 92, 22 };
    bool local = false;
    bool *ep = editing ? editing : &local;
    bool changed = UITextField(id, r, buf, cap, ep);
    if (changed) { ed->scene_dirty = true; ed->text_focus = *ep; }
    *y += 27;
    return changed;
}

static bool prop_float(Editor *ed, const char *id, float x, float *y, float w,
                       const char *label, float *value, float min, float max,
                       const char *fmt)
{
    Rectangle r = { x, *y, w, 22 };
    bool changed = UISliderFloat(id, r, label, value, min, max, fmt);
    if (changed) edit_drag(ed);
    *y += 27;
    return changed;
}

static bool prop_int(Editor *ed, const char *id, float x, float *y, float w,
                     const char *label, int *value, int min, int max, const char *fmt)
{
    Rectangle r = { x, *y, w, 22 };
    bool changed = UISliderInt(id, r, label, value, min, max, fmt);
    if (changed) edit_drag(ed);
    *y += 27;
    return changed;
}

static void prop_section(float x, float *y, float w, const char *title)
{
    Rectangle r = { x, *y, w, 20 };
    UISectionHeader(r, title);
    *y += 24;
}

static void inspector_scene(Editor *ed, float x, float *y, float w)
{
    UITheme *t = UIThemeGet();
    UIDrawText("SCENE", x, *y, 11, t->accent);
    *y += 18;
    prop_text(ed, "scene_name", x, y, w, "Name", ed->scene.name, SCENE_NAME_MAX, &ed->edit_scene_name);

    char info[128];
    snprintf(info, sizeof info, "%d entities   |   %s", ed->scene.count,
             ed->scene_dirty ? "unsaved changes" : "saved");
    UIDrawText(info, x, *y, 11, ed->scene_dirty ? t->warning : t->text_dim);
    *y += 20;
    char clipped[256];
    UIClipText(ed->scene_path, 10, w, clipped, sizeof clipped);
    UIDrawText(clipped, x, *y, 10, t->text_disabled);
    *y += 20;

    float half = (w - 8) * 0.5f;
    if (UIButton("scene_save_btn", (Rectangle){ x, *y, half, 26 }, "Save Scene", true))
        scene_save(ed);
    if (UIButton("scene_new_btn", (Rectangle){ x + half + 8, *y, half, 26 }, "New Scene", true))
        scene_new(ed);
    *y += 34;
    if (UIButton("scene_reload_btn", (Rectangle){ x, *y, w, 26 }, "Reload Assets  (F5)", true))
        reload_all(ed);
    *y += 38;

    prop_section(x, y, w, "SCENE CONTENT");
    int roots = 0, sprites = 0, colliders = 0;
    for (int i = 0; i < ed->scene.count; i++) {
        const Entity *e = SceneEntityConst(&ed->scene, i);
        if (!e) continue;
        if (e->parent < 0) roots++;
        if (e->has_sprite) sprites++;
        if (e->has_collider) colliders++;
    }
    char stats[160];
    snprintf(stats, sizeof stats, "%d root   %d sprites   %d colliders", roots, sprites, colliders);
    UIDrawText(stats, x, *y, 11, t->text_dim);
    *y += 20;
    UIDrawText("Select an entity in the Hierarchy to edit it.", x, *y, 11, t->text_disabled);
    *y += 26;
    if (UIButtonAccent("scene_add_btn", (Rectangle){ x, *y, w, 30 }, "+  Add Entity", true)) {
        undo_capture(ed);
        int at = SceneAdd(&ed->scene, "Entity");
        if (at >= 0) {
            Entity *e = SceneEntity(&ed->scene, at);
            e->position = ed->cam_center;
            e->sprite_size = (Vector2){ 48, 48 };
            e->collider_radius = 20;
            select_entity(ed, at);
            ed->scene_dirty = true;
            say(ed, true, "Added %s", e->name);
        }
    }
    *y += 40;
}

static void inspector_entity(Editor *ed, Entity *e, float x, float *y, float w)
{
    UITheme *t = UIThemeGet();
    char head[128];
    snprintf(head, sizeof head, "%s", e->name[0] ? e->name : "Entity");
    UIDrawText(head, x, *y, 14, t->text);
    const char *tag = e->tag[0] ? e->tag : "no tag";
    float tw = UITextWidth(tag, 10);
    UIDrawText(tag, x + w - tw, *y + 3, 10, entity_color(e));
    *y += 24;

    prop_section(x, y, w, "IDENTITY");
    prop_text(ed, "ent_name", x, y, w, "Name", e->name, SCENE_NAME_MAX, &ed->edit_name);
    prop_text(ed, "ent_tag", x, y, w, "Tag", e->tag, SCENE_TAG_MAX, &ed->edit_tag);

    char parent_labels[SCENE_MAX_ENTITIES + 1][SCENE_NAME_MAX];
    const char *parent_names[SCENE_MAX_ENTITIES + 1];
    int parent_indices[SCENE_MAX_ENTITIES + 1];
    parent_names[0] = "(none)";
    parent_indices[0] = -1;
    int pcount = 1;
    int pv = 0;
    for (int i = 0; i < ed->scene.count; i++) {
        if (i == ed->selected) continue;
        snprintf(parent_labels[pcount], SCENE_NAME_MAX, "%s", ed->scene.items[i].name);
        parent_names[pcount] = parent_labels[pcount];
        parent_indices[pcount] = i;
        if (i == e->parent) pv = pcount;
        pcount++;
    }
    Rectangle pr = { x, *y, w, 22 };
    if (UICycler("ent_parent", pr, "Parent", &pv, parent_names, pcount)) {
        int target = parent_indices[pv];
        if (!SceneSetParent(&ed->scene, ed->selected, target))
            say(ed, false, "That parent would create a cycle");
        else edit_drag(ed);
    }
    *y += 28;

    prop_section(x, y, w, "TRANSFORM");
    prop_float(ed, "ent_px", x, y, w, "Position X", &e->position.x, -4000, 5200, "%.1f");
    prop_float(ed, "ent_py", x, y, w, "Position Y", &e->position.y, -4000, 5200, "%.1f");
    prop_float(ed, "ent_rot", x, y, w, "Rotation", &e->rotation, -360, 360, "%.1f deg");
    prop_float(ed, "ent_sx", x, y, w, "Scale X", &e->scale.x, 0.01f, 20.0f, "%.2f");
    prop_float(ed, "ent_sy", x, y, w, "Scale Y", &e->scale.y, 0.01f, 20.0f, "%.2f");
    if (UIButton("ent_focus", (Rectangle){ x, *y, w, 24 }, "Focus in Viewport", true)) {
        ed->cam_center = SceneWorldPosition(&ed->scene, ed->selected);
        ed->cam_zoom = 1.0f;
    }
    *y += 32;

    prop_section(x, y, w, "RENDERING");
    Rectangle vis = { x, *y, w, 22 };
    if (UICheckbox("ent_visible", vis, "Visible", &e->visible)) edit_drag(ed);
    *y += 27;
    prop_int(ed, "ent_layer", x, y, w, "Layer", &e->layer, 0, 16, "%d");

    Rectangle spr = { x, *y, w, 22 };
    if (UICheckbox("ent_has_sprite", spr, "Sprite", &e->has_sprite)) edit_drag(ed);
    *y += 27;
    if (e->has_sprite) {
        prop_text(ed, "ent_sprite", x, y, w, "Texture", e->sprite, SCENE_SPRITE_MAX, &ed->edit_sprite);
        prop_float(ed, "ent_sw", x, y, w, "Size X", &e->sprite_size.x, 1, 1024, "%.0f px");
        prop_float(ed, "ent_sh", x, y, w, "Size Y", &e->sprite_size.y, 1, 1024, "%.0f px");
        Rectangle col = { x, *y, w, 22 };
        if (UIColorButton("ent_tint", col, "Tint", &e->tint)) edit_drag(ed);
        *y += 28;
    }

    prop_section(x, y, w, "COLLIDER");
    Rectangle coll = { x, *y, w, 22 };
    if (UICheckbox("ent_has_collider", coll, "Circle collider", &e->has_collider)) edit_drag(ed);
    *y += 27;
    if (e->has_collider) {
        prop_float(ed, "ent_radius", x, y, w, "Radius", &e->collider_radius, 1, 1000, "%.0f px");
        Rectangle trig = { x, *y, w, 22 };
        if (UICheckbox("ent_trigger", trig, "Trigger only", &e->collider_trigger)) edit_drag(ed);
        *y += 27;
    }

    prop_section(x, y, w, "ENTITY ACTIONS");
    float half = (w - 8) * 0.5f;
    if (UIButton("ent_dup", (Rectangle){ x, *y, half, 26 }, "Duplicate", true))
        duplicate_selected(ed);
    if (UIButton("ent_del", (Rectangle){ x + half + 8, *y, half, 26 }, "Delete", true))
        delete_selected(ed);
    *y += 34;
}

static void draw_inspector(Editor *ed)
{
    Layout *L = &ed->layout;
    UITheme *t = UIThemeGet();
    Rectangle body = UIPanelBegin(L->right, "INSPECTOR");
    if (ed->selected < 0 || ed->selected >= ed->scene.count) select_entity(ed, -1);

    Rectangle tabs = { body.x + 4, body.y + 3, body.width - 8, 24 };
    float half = tabs.width * 0.5f;
    if (UIFlatButton("insp_tab_scene", (Rectangle){ tabs.x, tabs.y, half, tabs.height },
                      "SCENE", ed->selected < 0)) select_entity(ed, -1);
    if (UIFlatButton("insp_tab_entity", (Rectangle){ tabs.x + half, tabs.y, half, tabs.height },
                      "ENTITY", ed->selected >= 0)) {
        if (ed->selected < 0 && ed->scene.count > 0) select_entity(ed, 0);
    }

    Rectangle view = { body.x + 2, body.y + 32, body.width - 4, body.height - 34 };
    float content = 980.0f;
    float scroll = UIScroll("inspector_scroll", view, content, &ed->inspector_scroll);
    UIPushClip(view);
    float x = view.x + 10;
    float y = view.y + 6 - scroll;
    float w = view.width - 22;

    if (ed->selected >= 0 && ed->selected < ed->scene.count)
        inspector_entity(ed, SceneEntity(&ed->scene, ed->selected), x, &y, w);
    else
        inspector_scene(ed, x, &y, w);

    UIPopClip();
    UIPanelEnd();
    (void)t;
}

/* ---------------- hierarchy ---------------- */
typedef struct {
    int index[SCENE_MAX_ENTITIES];
    int depth[SCENE_MAX_ENTITIES];
    bool has_child[SCENE_MAX_ENTITIES];
    int count;
} FlatList;

static bool entity_has_children(const Scene *s, int index)
{
    for (int i = 0; i < s->count; i++)
        if (s->items[i].parent == index) return true;
    return false;
}

static void flatten_children(Editor *ed, int parent, int depth, FlatList *out)
{
    if (depth > 32 || out->count >= SCENE_MAX_ENTITIES) return;
    for (int i = 0; i < ed->scene.count && out->count < SCENE_MAX_ENTITIES; i++) {
        Entity *e = &ed->scene.items[i];
        int p = e->parent;
        if (p < 0 || p >= ed->scene.count) p = -1;
        if (p != parent) continue;
        int at = out->count++;
        out->index[at] = i;
        out->depth[at] = depth;
        out->has_child[at] = entity_has_children(&ed->scene, i);
        if (out->has_child[at] && ed->expanded[i])
            flatten_children(ed, i, depth + 1, out);
    }
}

static void hierarchy_add(Editor *ed)
{
    undo_capture(ed);
    int at = SceneAdd(&ed->scene, "Entity");
    if (at < 0) { say(ed, false, "Scene is full (256 entities)"); return; }
    Entity *e = SceneEntity(&ed->scene, at);
    e->position = ed->cam_center;
    e->sprite_size = (Vector2){ 48, 48 };
    e->collider_radius = 20;
    select_entity(ed, at);
    ed->scene_dirty = true;
    say(ed, true, "Added %s", e->name);
}

static void draw_hierarchy(Editor *ed, Rectangle body)
{
    UITheme *t = UIThemeGet();
    float top = body.y + 4;
    float bh = 26;
    if (UIIconButton("hier_add", (Rectangle){ body.x + 4, top, 28, bh }, UI_ICON_PLUS,
                     false, true, "Add entity")) hierarchy_add(ed);
    if (UIIconButton("hier_dup", (Rectangle){ body.x + 36, top, 28, bh }, UI_ICON_DUPLICATE,
                     false, ed->selected >= 0, "Duplicate entity")) duplicate_selected(ed);
    if (UIIconButton("hier_del", (Rectangle){ body.x + 68, top, 28, bh }, UI_ICON_TRASH,
                     false, ed->selected >= 0, "Delete entity")) delete_selected(ed);
    UIDrawText(TextFormat("%d", ed->scene.count), body.x + 104, top + 7, 11, t->text_disabled);

    Rectangle view = { body.x + 2, top + 32, body.width - 4, body.height - 38 };
    FlatList flat;
    flat.count = 0;
    flatten_children(ed, -1, 0, &flat);
    float content = flat.count * 24.0f + 8.0f;
    float scroll = UIScroll("hier_scroll", view, content, &ed->hier_scroll);
    UIPushClip(view);
    float y = view.y + 3 - scroll;

    for (int i = 0; i < flat.count; i++) {
        int idx = flat.index[i];
        Entity *e = &ed->scene.items[idx];
        char id[32];
        snprintf(id, sizeof id, "hier_%d", idx);
        Rectangle row = { view.x + 2, y, view.width - 10, 23 };
        bool has_children = entity_has_children(&ed->scene, idx);
        bool expanded = ed->expanded[idx];
        if (UITreeRow(id, row, flat.depth[i], e->name, ed->selected == idx,
                      has_children, &expanded, entity_icon(e), entity_color(e))) {
            select_entity(ed, idx);
        }
        ed->expanded[idx] = expanded;
        y += 24;
    }
    if (flat.count == 0)
        UIDrawText("No entities - press + to add one", view.x + 12, view.y + 10, 11, t->text_disabled);
    UIPopClip();
}

/* ---------------- assets ---------------- */
static const AssetNameList *asset_list(const Editor *ed)
{
    switch (ed->asset_tab) {
    case 0: return &ed->textures;
    case 1: return &ed->scripts;
    case 2: return &ed->data;
    default: return &ed->scene_files;
    }
}

static void asset_action(Editor *ed)
{
    if (!ed->selected_asset[0]) return;
    if (ed->asset_tab == 0) {
        if (ed->selected >= 0 && ed->selected < ed->scene.count) {
            Entity *e = SceneEntity(&ed->scene, ed->selected);
            undo_capture(ed);
            e->has_sprite = true;
            snprintf(e->sprite, sizeof e->sprite, "%.95s", ed->selected_asset);
            if (e->sprite_size.x <= 1 && e->sprite_size.y <= 1) e->sprite_size = (Vector2){ 64, 64 };
            ed->scene_dirty = true;
            say(ed, true, "Assigned %s to %s", ed->selected_asset, e->name);
        } else say(ed, false, "Select an entity first");
    } else if (ed->asset_tab == 3) {
        char path[4096];
        if (EngineResolveResource(TextFormat("scenes/%s", ed->selected_asset), path, sizeof path))
            scene_load(ed, path);
        else say(ed, false, "Scene not found: %s", ed->selected_asset);
    } else if (ed->asset_tab == 2) {
        if (strcmp(ed->selected_asset, "patterns.txt") == 0) patterns_load_editor(ed);
        else if (strcmp(ed->selected_asset, "levels.txt") == 0) levels_load_editor(ed);
        else say(ed, true, "%s is a data file", ed->selected_asset);
    } else {
        reload_all(ed);
    }
}

static void draw_assets(Editor *ed, Rectangle body)
{
    UITheme *t = UIThemeGet();
    const char *labels[4] = { "TEX", "LUA", "DATA", "SCENE" };
    float tw = (body.width - 10) / 4.0f;
    for (int i = 0; i < 4; i++) {
        if (UIFlatButton(TextFormat("asset_tab_%d", i),
                         (Rectangle){ body.x + 4 + i * tw, body.y + 3, tw - 2, 24 },
                         labels[i], ed->asset_tab == i))
            ed->asset_tab = i;
    }

    const AssetNameList *list = asset_list(ed);
    Rectangle view = { body.x + 2, body.y + 34, body.width - 4, body.height - 154 };
    float content = list->count * 24.0f + 6;
    float scroll = UIScroll("asset_scroll", view, content, &ed->asset_scroll);
    UIPushClip(view);
    float y = view.y + 3 - scroll;
    for (int i = 0; i < list->count; i++) {
        const char *name = list->items[i];
        Rectangle row = { view.x + 2, y, view.width - 10, 23 };
        UIIcon icon = ed->asset_tab == 0 ? UI_ICON_TEXTURE :
                      ed->asset_tab == 1 ? UI_ICON_SCRIPT :
                      ed->asset_tab == 2 ? UI_ICON_SETTINGS : UI_ICON_FOLDER;
        if (UISelectable(TextFormat("asset_%d", i), row, name,
                         strcmp(name, ed->selected_asset) == 0, icon, t->accent))
            snprintf(ed->selected_asset, sizeof ed->selected_asset, "%s", name);
        y += 24;
    }
    if (list->count == 0)
        UIDrawText("No assets in this category", view.x + 12, view.y + 10, 11, t->text_disabled);
    UIPopClip();

    Rectangle preview = { body.x + 4, body.y + body.height - 112, body.width - 8, 72 };
    DrawRectangleRounded(preview, 0.1f, 6, (Color){ 18, 21, 26, 255 });
    DrawRectangleRoundedLinesEx(preview, 0.1f, 6, 1.0f, t->border);
    if (ed->selected_asset[0]) {
        if (ed->asset_tab == 0) {
            Texture2D tex = AssetsTexture(ed->selected_asset);
            if (tex.id != 0) {
                float z = fminf((preview.width - 12) / tex.width, (preview.height - 12) / tex.height);
                Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
                Rectangle dst = { preview.x + (preview.width - tex.width * z) * 0.5f,
                                  preview.y + (preview.height - tex.height * z) * 0.5f,
                                  tex.width * z, tex.height * z };
                DrawTexturePro(tex, src, dst, (Vector2){ 0, 0 }, 0, WHITE);
            } else {
                UIDrawText("texture missing", preview.x + 10, preview.y + 28, 11, t->error);
            }
        } else {
            char clipped[ASSET_NAME_MAX];
            UIClipText(ed->selected_asset, 12, preview.width - 20, clipped, sizeof clipped);
            UIDrawText(clipped, preview.x + 10, preview.y + 26, 12, t->text);
        }
    } else {
        UIDrawText("Select an asset", preview.x + 10, preview.y + 28, 11, t->text_disabled);
    }

    Rectangle btn = { body.x + 4, body.y + body.height - 34, body.width - 8, 26 };
    const char *action = "Reload Assets";
    bool enabled = true;
    if (ed->asset_tab == 0) { action = "Assign to Selected Sprite"; enabled = ed->selected >= 0; }
    else if (ed->asset_tab == 3) action = "Open Selected Scene";
    else if (ed->asset_tab == 1) action = "Reload Scripts";
    else action = "Open Data File";
    if (UIButtonAccent("asset_action", btn, action, enabled)) asset_action(ed);
}

static void draw_left(Editor *ed)
{
    Layout *L = &ed->layout;
    Rectangle body = UIPanelBegin(L->left, NULL);
    Rectangle tabs = { body.x + 4, body.y + 3, body.width - 8, 25 };
    float half = tabs.width * 0.5f;
    if (UIFlatButton("left_tab_hier", (Rectangle){ tabs.x, tabs.y, half, tabs.height },
                      "HIERARCHY", ed->left_tab == LEFT_HIERARCHY))
        ed->left_tab = LEFT_HIERARCHY;
    if (UIFlatButton("left_tab_assets", (Rectangle){ tabs.x + half, tabs.y, half, tabs.height },
                      "ASSETS", ed->left_tab == LEFT_ASSETS))
        ed->left_tab = LEFT_ASSETS;
    Rectangle content = { body.x, body.y + 32, body.width, body.height - 34 };
    if (ed->left_tab == LEFT_HIERARCHY) draw_hierarchy(ed, content);
    else draw_assets(ed, content);
    UIPanelEnd();
}

/* ---------------- viewport ---------------- */
static Vector2 scene_to_screen(const Editor *ed, Rectangle body, Vector2 p)
{
    return (Vector2){ body.x + body.width * 0.5f + (p.x - ed->cam_center.x) * ed->cam_zoom,
                      body.y + body.height * 0.5f + (p.y - ed->cam_center.y) * ed->cam_zoom };
}

static Vector2 screen_to_scene(const Editor *ed, Rectangle body, Vector2 p)
{
    return (Vector2){ ed->cam_center.x + (p.x - body.x - body.width * 0.5f) / ed->cam_zoom,
                      ed->cam_center.y + (p.y - body.y - body.height * 0.5f) / ed->cam_zoom };
}

static Vector2 rotate2(Vector2 v, float radians)
{
    float c = cosf(radians), s = sinf(radians);
    return (Vector2){ v.x * c - v.y * s, v.x * s + v.y * c };
}

static int pick_entity(Editor *ed, Vector2 world)
{
    for (int i = ed->scene.count - 1; i >= 0; i--) {
        const Entity *e = SceneEntityConst(&ed->scene, i);
        if (!e) continue;
        Vector2 pos = SceneWorldPosition(&ed->scene, i);
        float rot = SceneWorldRotation(&ed->scene, i) * DEG2RAD;
        Vector2 scale = SceneWorldScale(&ed->scene, i);
        Vector2 local = rotate2((Vector2){ world.x - pos.x, world.y - pos.y }, -rot);
        if (strcmp(e->tag, "arena") == 0) {
            Vector2 size = { e->scale.x * scale.x, e->scale.y * scale.y };
            if (local.x >= 0 && local.y >= 0 && local.x <= size.x && local.y <= size.y) return i;
            continue;
        }
        Vector2 size = e->has_sprite ? e->sprite_size : (Vector2){ 32, 32 };
        size.x *= scale.x; size.y *= scale.y;
        float hx = fmaxf(size.x * 0.5f, 8.0f), hy = fmaxf(size.y * 0.5f, 8.0f);
        if (fabsf(local.x) <= hx && fabsf(local.y) <= hy) return i;
    }
    return -1;
}

static void draw_rotated_outline(Vector2 center, Vector2 size, float rotation, Color color, float thick)
{
    float r = rotation * DEG2RAD;
    Vector2 half = { size.x * 0.5f, size.y * 0.5f };
    Vector2 pts[4] = { { -half.x, -half.y }, { half.x, -half.y }, { half.x, half.y }, { -half.x, half.y } };
    Vector2 w[4];
    for (int i = 0; i < 4; i++) {
        Vector2 p = rotate2(pts[i], r);
        w[i] = (Vector2){ center.x + p.x, center.y + p.y };
    }
    for (int i = 0; i < 4; i++) DrawLineEx(w[i], w[(i + 1) % 4], thick, color);
}

static void draw_entity(Editor *ed, Rectangle body, int index, bool selected)
{
    UITheme *t = UIThemeGet();
    const Entity *e = SceneEntityConst(&ed->scene, index);
    if (!e) return;
    Vector2 pos = SceneWorldPosition(&ed->scene, index);
    float rot = SceneWorldRotation(&ed->scene, index);
    Vector2 scale = SceneWorldScale(&ed->scene, index);
    Vector2 sp = scene_to_screen(ed, body, pos);
    Color tint = e->tint;
    if (!e->visible) tint.a = 90;

    if (strcmp(e->tag, "arena") == 0) {
        Vector2 size = { e->scale.x * scale.x, e->scale.y * scale.y };
        Rectangle r = { sp.x, sp.y, size.x * ed->cam_zoom, size.y * ed->cam_zoom };
        DrawRectangleRec(r, Fade((Color){ 70, 110, 180, 255 }, 0.06f));
        DrawRectangleLinesEx(r, 1.5f, selected ? t->selection_edge : (Color){ 90, 140, 220, 150 });
        UIDrawText("ARENA", r.x + 8, r.y + 6, 10, (Color){ 100, 150, 220, 170 });
        return;
    }

    if (e->has_sprite && e->sprite[0]) {
        Texture2D tex = AssetsTexture(e->sprite);
        if (tex.id != 0) {
            Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
            Rectangle dst = { sp.x, sp.y, e->sprite_size.x * scale.x * ed->cam_zoom,
                              e->sprite_size.y * scale.y * ed->cam_zoom };
            DrawTexturePro(tex, src, dst, (Vector2){ dst.width * 0.5f, dst.height * 0.5f }, rot, tint);
            if (selected) draw_rotated_outline(sp, (Vector2){ dst.width, dst.height }, rot, t->selection_edge, 1.5f);
            return;
        }
        UIDrawText("?", sp.x - 3, sp.y - 7, 13, t->error);
    }

    if (strcmp(e->tag, "player_spawn") == 0) {
        float rad = 14.0f * fmaxf(scale.x, scale.y) * ed->cam_zoom;
        DrawCircleV(sp, rad, Fade((Color){ 70, 220, 140, 255 }, 0.14f));
        DrawCircleLinesV(sp, rad, selected ? t->selection_edge : (Color){ 80, 220, 150, 230 });
        DrawLineEx((Vector2){ sp.x - rad, sp.y }, (Vector2){ sp.x + rad, sp.y }, 1, (Color){ 80, 220, 150, 190 });
        DrawLineEx((Vector2){ sp.x, sp.y - rad }, (Vector2){ sp.x, sp.y + rad }, 1, (Color){ 80, 220, 150, 190 });
        UIDrawText("SPAWN", sp.x + rad + 5, sp.y - 6, 10, (Color){ 90, 210, 150, 190 });
    } else {
        Vector2 size = { 32 * scale.x * ed->cam_zoom, 32 * scale.y * ed->cam_zoom };
        Rectangle dst = { sp.x, sp.y, size.x, size.y };
        DrawRectanglePro(dst, (Vector2){ size.x * 0.5f, size.y * 0.5f }, rot,
                         Fade(entity_color(e), e->visible ? 0.28f : 0.12f));
        draw_rotated_outline(sp, size, rot, selected ? t->selection_edge : Fade(entity_color(e), 0.75f), 1.2f);
    }

    if (e->has_collider) {
        float rad = e->collider_radius * fmaxf(scale.x, scale.y) * ed->cam_zoom;
        DrawCircleLinesV(sp, rad, e->collider_trigger ? (Color){ 230, 180, 70, 180 } : (Color){ 230, 100, 110, 150 });
    }

    if (selected || ed->cam_zoom > 0.75f) {
        char label[96];
        snprintf(label, sizeof label, "%s%s", e->name, e->visible ? "" : "  (hidden)");
        UIDrawText(label, sp.x + 12, sp.y + 8, 10, selected ? t->text : t->text_disabled);
    }
}

static void draw_grid(Editor *ed, Rectangle body)
{
    UITheme *t = UIThemeGet();
    float step = 50.0f;
    while (step * ed->cam_zoom < 28.0f) step *= 2.0f;
    while (step * ed->cam_zoom > 190.0f) step *= 0.5f;
    Vector2 tl = screen_to_scene(ed, body, (Vector2){ body.x, body.y });
    Vector2 br = screen_to_scene(ed, body, (Vector2){ body.x + body.width, body.y + body.height });
    float x0 = floorf(tl.x / step) * step;
    float y0 = floorf(tl.y / step) * step;
    int guard = 0;
    for (float x = x0; x <= br.x && guard < 500; x += step, guard++) {
        Vector2 a = scene_to_screen(ed, body, (Vector2){ x, tl.y });
        Vector2 b = scene_to_screen(ed, body, (Vector2){ x, br.y });
        bool major = fabsf(fmodf(x, step * 5.0f)) < 0.01f;
        DrawLineEx(a, b, major ? 1.2f : 1.0f, major ? t->grid_major : t->grid);
    }
    guard = 0;
    for (float y = y0; y <= br.y && guard < 500; y += step, guard++) {
        Vector2 a = scene_to_screen(ed, body, (Vector2){ tl.x, y });
        Vector2 b = scene_to_screen(ed, body, (Vector2){ br.x, y });
        bool major = fabsf(fmodf(y, step * 5.0f)) < 0.01f;
        DrawLineEx(a, b, major ? 1.2f : 1.0f, major ? t->grid_major : t->grid);
    }
    Vector2 origin = scene_to_screen(ed, body, (Vector2){ 0, 0 });
    if (origin.x > body.x && origin.x < body.x + body.width)
        DrawLineEx((Vector2){ origin.x, body.y }, (Vector2){ origin.x, body.y + body.height }, 1.0f, Fade(t->accent, 0.22f));
    if (origin.y > body.y && origin.y < body.y + body.height)
        DrawLineEx((Vector2){ body.x, origin.y }, (Vector2){ body.x + body.width, origin.y }, 1.0f, Fade(t->accent, 0.22f));
}

static void draw_gizmo(Editor *ed, Rectangle body, const Entity *e)
{
    if (!e || ed->tool == TOOL_SELECT) return;
    UITheme *t = UIThemeGet();
    Vector2 sp = scene_to_screen(ed, body, SceneWorldPosition(&ed->scene, ed->selected));
    float len = 72.0f;
    Vector2 x_end = { sp.x + len, sp.y };
    Vector2 y_end = { sp.x, sp.y + len };

    if (ed->tool == TOOL_MOVE || ed->tool == TOOL_SCALE) {
        DrawLineEx(sp, x_end, 2.0f, (Color){ 235, 90, 100, 230 });
        DrawLineEx(sp, y_end, 2.0f, (Color){ 100, 210, 130, 230 });
        DrawTriangle((Vector2){ x_end.x + 9, x_end.y }, (Vector2){ x_end.x - 2, x_end.y - 5 },
                     (Vector2){ x_end.x - 2, x_end.y + 5 }, (Color){ 235, 90, 100, 230 });
        DrawTriangle((Vector2){ y_end.x, y_end.y + 9 }, (Vector2){ y_end.x - 5, y_end.y - 2 },
                     (Vector2){ y_end.x + 5, y_end.y - 2 }, (Color){ 100, 210, 130, 230 });
        if (ed->tool == TOOL_SCALE) {
            DrawRectangleRec((Rectangle){ x_end.x - 4, x_end.y - 4, 8, 8 }, (Color){ 235, 90, 100, 230 });
            DrawRectangleRec((Rectangle){ y_end.x - 4, y_end.y - 4, 8, 8 }, (Color){ 100, 210, 130, 230 });
        }
        UIDrawText("X", x_end.x + 8, x_end.y - 14, 11, (Color){ 235, 90, 100, 230 });
        UIDrawText("Y", y_end.x + 8, y_end.y + 4, 11, (Color){ 100, 210, 130, 230 });
    } else if (ed->tool == TOOL_ROTATE) {
        DrawCircleLinesV(sp, 58, Fade(t->accent, 0.85f));
        DrawCircleLinesV(sp, 56, Fade(t->accent, 0.25f));
        for (int i = 0; i < 12; i++) {
            float a = i * 30.0f * DEG2RAD;
            Vector2 inner = { sp.x + cosf(a) * 49, sp.y + sinf(a) * 49 };
            Vector2 outer = { sp.x + cosf(a) * 58, sp.y + sinf(a) * 58 };
            DrawLineEx(inner, outer, i % 3 == 0 ? 2.0f : 1.0f, Fade(t->accent, 0.7f));
        }
    }
}

static void handle_viewport_input(Editor *ed, Rectangle body)
{
    if (!UIMouseIn(body)) return;
    Vector2 mouse = GetMousePosition();
    if (mouse.y < body.y + 38) return;
    Vector2 world = screen_to_scene(ed, body, mouse);

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        Vector2 before = world;
        ed->cam_zoom *= powf(1.12f, wheel);
        ed->cam_zoom = clampf(ed->cam_zoom, 0.08f, 8.0f);
        Vector2 after = screen_to_scene(ed, body, mouse);
        ed->cam_center.x += before.x - after.x;
        ed->cam_center.y += before.y - after.y;
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 d = GetMouseDelta();
        ed->cam_center.x -= d.x / ed->cam_zoom;
        ed->cam_center.y -= d.y / ed->cam_zoom;
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Entity *e = (ed->selected >= 0) ? SceneEntity(&ed->scene, ed->selected) : NULL;
        if (e) {
            Vector2 sp = scene_to_screen(ed, body, SceneWorldPosition(&ed->scene, ed->selected));
            float len = 72.0f;
            bool near_x = fabsf(mouse.y - sp.y) < 9 && mouse.x > sp.x + 18 && mouse.x < sp.x + len + 14;
            bool near_y = fabsf(mouse.x - sp.x) < 9 && mouse.y > sp.y + 18 && mouse.y < sp.y + len + 14;
            if (ed->tool == TOOL_MOVE && near_x) { ed->scene_drag = 1; ed->drag_mouse = world; ed->drag_pos = e->position; return; }
            if (ed->tool == TOOL_MOVE && near_y) { ed->scene_drag = 2; ed->drag_mouse = world; ed->drag_pos = e->position; return; }
            if (ed->tool == TOOL_SCALE && near_x) { ed->scene_drag = 4; ed->drag_mouse = world; ed->drag_scale = e->scale; return; }
            if (ed->tool == TOOL_SCALE && near_y) { ed->scene_drag = 5; ed->drag_mouse = world; ed->drag_scale = e->scale; return; }
            if (ed->tool == TOOL_ROTATE) {
                float d = sqrtf((mouse.x - sp.x) * (mouse.x - sp.x) + (mouse.y - sp.y) * (mouse.y - sp.y));
                if (d > 45 && d < 72) { ed->scene_drag = 3; ed->drag_mouse = world; ed->drag_rot = e->rotation; return; }
            }
        }

        int hit = pick_entity(ed, world);
        select_entity(ed, hit);
        if (hit >= 0 && ed->tool == TOOL_MOVE) {
            Entity *he = SceneEntity(&ed->scene, hit);
            ed->scene_drag = 6;
            ed->drag_mouse = world;
            ed->drag_pos = he->position;
        }
    }

    if (ed->scene_drag >= 1 && ed->scene_drag <= 6 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Entity *e = SceneEntity(&ed->scene, ed->selected);
        if (!e) { ed->scene_drag = 0; return; }
        if (ed->scene_drag == 1 || ed->scene_drag == 2 || ed->scene_drag == 6) {
            float nx = ed->drag_pos.x + (world.x - ed->drag_mouse.x);
            float ny = ed->drag_pos.y + (world.y - ed->drag_mouse.y);
            if (ed->snap) {
                if (ed->scene_drag != 2) nx = snap_value(nx, ed->snap_size);
                if (ed->scene_drag != 1) ny = snap_value(ny, ed->snap_size);
            }
            if (ed->scene_drag != 2) e->position.x = nx;
            if (ed->scene_drag != 1) e->position.y = ny;
            edit_drag(ed);
        } else if (ed->scene_drag == 3) {
            Vector2 wp = SceneWorldPosition(&ed->scene, ed->selected);
            float a0 = atan2f(world.y - wp.y, world.x - wp.x);
            float a1 = atan2f(ed->drag_mouse.y - wp.y, ed->drag_mouse.x - wp.x);
            float deg = ed->drag_rot + (a0 - a1) * RAD2DEG;
            if (ed->snap) deg = snap_value(deg, 15.0f);
            e->rotation = deg;
            edit_drag(ed);
        } else if (ed->scene_drag == 4) {
            float f = 1.0f + (world.x - ed->drag_mouse.x) / 100.0f;
            e->scale.x = clampf(ed->drag_scale.x * f, 0.01f, 20.0f);
            if (ed->snap) e->scale.x = snap_value(e->scale.x, 0.1f);
            edit_drag(ed);
        } else if (ed->scene_drag == 5) {
            float f = 1.0f + (world.y - ed->drag_mouse.y) / 100.0f;
            e->scale.y = clampf(ed->drag_scale.y * f, 0.01f, 20.0f);
            if (ed->snap) e->scale.y = snap_value(e->scale.y, 0.1f);
            edit_drag(ed);
        }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) ed->scene_drag = 0;
}

static void draw_viewport(Editor *ed)
{
    Layout *L = &ed->layout;
    UITheme *t = UIThemeGet();
    Rectangle body = UIPanelBegin(L->viewport, NULL);
    Rectangle inner = { body.x + 1, body.y + 1, body.width - 2, body.height - 2 };
    DrawRectangleRec(inner, t->viewport);

    if (ed->playing && ed->game && ed->play_rt_ready) {
        /* The UI clip is in screen coordinates. If it stays active while the
           game renders into its own 1200x600 target, the target is clipped
           by the viewport's screen offset and the game appears cut in half. */
        Rectangle clip = UIClipRect();
        EndScissorMode();
        BeginTextureMode(ed->play_rt);
        ClearBackground(BLACK);
        GameDraw(ed->game);
        EndTextureMode();
        BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);

        Vector2 origin; float scale;
        Rectangle dest = play_dest(ed, &origin, &scale);
        DrawRectangleRec(inner, (Color){ 10, 12, 16, 255 });
        DrawTexturePro(ed->play_rt.texture,
                       (Rectangle){ 0, 0, (float)BASE_W, -(float)BASE_H },
                       dest, (Vector2){ 0, 0 }, 0, WHITE);
        DrawRectangleLinesEx(dest, 1.0f, Fade(t->accent, 0.55f));
        if (ed->paused) {
            DrawRectangleRec(inner, Fade(BLACK, 0.25f));
            UIDrawTextCentered("PAUSED", inner, 24, t->warning);
        }
        (void)origin; (void)scale;
    } else {
        if (ed->show_grid) draw_grid(ed, inner);
        for (int i = 0; i < ed->scene.count; i++)
            draw_entity(ed, inner, i, i == ed->selected);
        if (ed->selected >= 0 && ed->selected < ed->scene.count)
            draw_gizmo(ed, inner, SceneEntityConst(&ed->scene, ed->selected));
        handle_viewport_input(ed, inner);
    }

    Rectangle tab = { inner.x + 8, inner.y + 8, 58, 22 };
    if (UIFlatButton("vp_scene_tab", tab, "Scene", !ed->playing)) {
        if (ed->playing) { play_stop(ed); say(ed, true, "Back to scene view"); }
    }
    Rectangle tab2 = { tab.x + tab.width + 2, tab.y, 58, 22 };
    if (UIFlatButton("vp_game_tab", tab2, "Game", ed->playing)) {
        if (!ed->playing) play_start(ed);
    }

    Rectangle fit = { inner.x + inner.width - 30, inner.y + 8, 22, 22 };
    if (UIIconButton("vp_fit", fit, UI_ICON_CAMERA, false, true, "Frame arena")) {
        ed->view_ready = false;
        layout_apply(ed);
    }
    const char *zoom = TextFormat("%.0f%%", ed->cam_zoom * 100.0f);
    UIDrawText(zoom, fit.x - UITextWidth(zoom, 11) - 10, fit.y + 5, 11, t->text_disabled);

    if (ed->playing && ed->paused) {
        Rectangle badge = { inner.x + inner.width * 0.5f - 42, inner.y + 10, 84, 22 };
        DrawRectangleRounded(badge, 0.3f, 6, Fade(t->warning, 0.18f));
        UIDrawTextCentered("PAUSED", badge, 11, t->warning);
    }
    UIPanelEnd();
}

/* ---------------- console tab ---------------- */
static void console_execute(Editor *ed)
{
    if (!ed->console_input[0]) return;
    if (ed->console_history_count < CONSOLE_HISTORY) {
        snprintf(ed->console_history[ed->console_history_count], 256, "%s", ed->console_input);
        ed->console_history_count++;
    } else {
        memmove(ed->console_history[0], ed->console_history[1], 256 * (CONSOLE_HISTORY - 1));
        snprintf(ed->console_history[CONSOLE_HISTORY - 1], 256, "%s", ed->console_input);
    }
    ed->console_history_pos = ed->console_history_count;
    ConsoleExec(ed->console_input);
    ed->console_input[0] = '\0';
    ed->console_editing = false;
    ed->console_follow = true;
}

static Color console_color(ConsoleLevel level)
{
    UITheme *t = UIThemeGet();
    switch (level) {
    case CONSOLE_WARN: return t->warning;
    case CONSOLE_ERROR: return t->error;
    case CONSOLE_COMMAND: return t->accent;
    case CONSOLE_OUTPUT: return t->success;
    default: return t->text;
    }
}

static void draw_console(Editor *ed, Rectangle content)
{
    UITheme *t = UIThemeGet();
    Rectangle input = { content.x + 4, content.y + content.height - 30, content.width - 8, 26 };
    Rectangle view = { content.x + 4, content.y + 4, content.width - 8, content.height - 38 };
    int n = ConsoleLineCount();
    float line_h = 16.0f;
    float content_h = n * line_h + 8.0f;
    float max_scroll = content_h - view.height;
    if (max_scroll < 0) max_scroll = 0;
    if (ed->console_follow) ed->console_scroll = max_scroll;
    float scroll = UIScroll("console_scroll", view, content_h, &ed->console_scroll);
    if (max_scroll - scroll < 2.0f) ed->console_follow = true;

    UIPushClip(view);
    DrawRectangleRec(view, (Color){ 17, 20, 25, 255 });
    float y = view.y + 4 - scroll;
    char clipped[CONSOLE_LINE_MAX + 8];
    for (int i = 0; i < n; i++) {
        ConsoleLevel lvl = CONSOLE_INFO;
        const char *text = ConsoleLine(i, &lvl);
        if (!text) continue;
        const char *prefix = lvl == CONSOLE_WARN ? "! " :
                             lvl == CONSOLE_ERROR ? "x " :
                             lvl == CONSOLE_COMMAND ? "> " : "  ";
        char full[CONSOLE_LINE_MAX + 8];
        snprintf(full, sizeof full, "%s%s", prefix, text);
        UIClipText(full, 12, view.width - 26, clipped, sizeof clipped);
        UIDrawText(clipped, view.x + 8, y, 12, console_color(lvl));
        y += line_h;
    }
    UIPopClip();

    if (ed->console_editing) {
        if (IsKeyPressed(KEY_UP) && ed->console_history_count > 0) {
            if (ed->console_history_pos > 0) ed->console_history_pos--;
            snprintf(ed->console_input, sizeof ed->console_input, "%s",
                     ed->console_history[ed->console_history_pos]);
        }
        if (IsKeyPressed(KEY_DOWN) && ed->console_history_count > 0) {
            if (ed->console_history_pos < ed->console_history_count - 1) {
                ed->console_history_pos++;
                snprintf(ed->console_input, sizeof ed->console_input, "%s",
                         ed->console_history[ed->console_history_pos]);
            } else {
                ed->console_history_pos = ed->console_history_count;
                ed->console_input[0] = '\0';
            }
        }
    }

    DrawRectangleRounded(input, 0.2f, 5, (Color){ 15, 18, 23, 255 });
    DrawRectangleRoundedLinesEx(input, 0.2f, 5, 1.0f, ed->console_editing ? t->accent : t->border);
    UIDrawText(">", input.x + 8, input.y + 6, 12, t->accent);
    if (UITextField("console_input", (Rectangle){ input.x + 22, input.y + 3, input.width - 72, 20 },
                    ed->console_input, sizeof ed->console_input, &ed->console_editing)) {
        ed->console_follow = true;
    }
    if (UIButtonAccent("console_run", (Rectangle){ input.x + input.width - 48, input.y + 3, 44, 20 },
                       "Run", ed->console_input[0] != 0))
        console_execute(ed);
    if (IsKeyPressed(KEY_ENTER) && !ed->console_editing) console_execute(ed);
    if (!ed->console_editing && !ed->console_input[0])
        UIDrawText("Type a command - try 'help'", input.x + 26, input.y + 7, 11, t->text_disabled);
}

/* ---------------- pattern tab ---------------- */
static void pattern_changed(Editor *ed)
{
    ed->pattern_dirty = true;
    preview_reset(ed);
}

static bool pat_float(Editor *ed, const char *id, float x, float *y, float w,
                     const char *label, float *value, float min, float max, const char *fmt)
{
    bool changed = UISliderFloat(id, (Rectangle){ x, *y, w, 22 }, label, value, min, max, fmt);
    if (changed) pattern_changed(ed);
    *y += 27;
    return changed;
}

static bool pat_int(Editor *ed, const char *id, float x, float *y, float w,
                    const char *label, int *value, int min, int max, const char *fmt)
{
    bool changed = UISliderInt(id, (Rectangle){ x, *y, w, 22 }, label, value, min, max, fmt);
    if (changed) pattern_changed(ed);
    *y += 27;
    return changed;
}

static bool pat_text(Editor *ed, const char *id, float x, float *y, float w,
                     const char *label, char *buf, int cap)
{
    bool dummy = false;
    UIDrawText(label, x, *y + 6, 12, UIThemeGet()->text_dim);
    bool changed = UITextField(id, (Rectangle){ x + 92, *y, w - 92, 22 }, buf, cap, &dummy);
    if (changed) pattern_changed(ed);
    *y += 27;
    return changed;
}

static void pattern_add(Editor *ed)
{
    if (ed->patterns.count >= MAX_PATTERNS) { say(ed, false, "Pattern table is full"); return; }
    int at = ed->patterns.count;
    Pattern p = (Pattern){ 0 };
    if (ed->pattern_sel >= 0 && ed->pattern_sel < ed->patterns.count)
        p = ed->patterns.items[ed->pattern_sel];
    p.name = NULL;
    p = pattern_apply_defaults(p);
    snprintf(ed->patterns.names[at], PATTERN_NAME_MAX, "new_pattern");
    p.name = ed->patterns.names[at];
    ed->patterns.items[at] = p;
    ed->patterns.count++;
    ed->pattern_sel = at;
    ed->pattern_dirty = true;
    preview_reset(ed);
    say(ed, true, "Added pattern %s", p.name);
}

static void pattern_delete(Editor *ed)
{
    if (ed->patterns.count <= 1) { say(ed, false, "Keep at least one pattern"); return; }
    int at = ed->pattern_sel;
    if (at < 0 || at >= ed->patterns.count) at = 0;
    for (int i = at; i < ed->patterns.count - 1; i++) {
        ed->patterns.items[i] = ed->patterns.items[i + 1];
        memcpy(ed->patterns.names[i], ed->patterns.names[i + 1], PATTERN_NAME_MAX);
        ed->patterns.items[i].name = ed->patterns.names[i];
    }
    ed->patterns.count--;
    if (ed->pattern_sel >= ed->patterns.count) ed->pattern_sel = ed->patterns.count - 1;
    ed->pattern_dirty = true;
    preview_reset(ed);
    say(ed, true, "Deleted pattern");
}

static void pattern_preview_draw(Editor *ed, Rectangle r)
{
    UITheme *t = UIThemeGet();
    DrawRectangleRec(r, (Color){ 13, 16, 21, 255 });
    DrawRectangleLinesEx(r, 1.0f, t->border);
    for (int i = 25; i < 100; i += 25) {
        float x = r.x + r.width * i / 100.0f;
        float y = r.y + r.height * i / 100.0f;
        DrawLineEx((Vector2){ x, r.y }, (Vector2){ x, r.y + r.height }, 1, Fade(t->grid, 0.5f));
        DrawLineEx((Vector2){ r.x, y }, (Vector2){ r.x + r.width, y }, 1, Fade(t->grid, 0.5f));
    }
    float scale = fminf(r.width / BASE_W, r.height / BASE_H);
    float ox = r.x + (r.width - BASE_W * scale) * 0.5f;
    float oy = r.y + (r.height - BASE_H * scale) * 0.5f;
    for (int i = 0; i < ed->preview.count; i++) {
        const Bullet *b = &ed->preview.items[i];
        if (b->dead) continue;
        Vector2 p = { ox + b->x * scale, oy + b->y * scale };
        float rad = fmaxf(b->radius * scale, 1.6f);
        DrawCircleV(p, rad, Fade(b->color, 0.95f));
    }
    if (ed->preview.count == 0)
        UIDrawText("preview", r.x + 10, r.y + 10, 10, t->text_disabled);
    UIDrawText(TextFormat("%d bullets", ed->preview.count), r.x + 8, r.y + r.height - 18, 10, t->text_disabled);
}

static void draw_pattern_props(Editor *ed, Rectangle area)
{
    if (ed->patterns.count <= 0) return;
    if (ed->pattern_sel < 0) ed->pattern_sel = 0;
    if (ed->pattern_sel >= ed->patterns.count) ed->pattern_sel = ed->patterns.count - 1;
    Pattern *p = &ed->patterns.items[ed->pattern_sel];
    p->name = ed->patterns.names[ed->pattern_sel];

    float scroll = UIScroll("pattern_props", area, 920.0f, &ed->pattern_prop_scroll);
    UIPushClip(area);
    float x = area.x + 10;
    float w = area.width - 24;
    float y = area.y + 4 - scroll;
    UIDrawText(TextFormat("PATTERN %d / %d", ed->pattern_sel + 1, ed->patterns.count),
               x, y, 10, UIThemeGet()->accent);
    y += 16;
    if (UITextField("pat_name", (Rectangle){ x, y, w, 22 }, ed->patterns.names[ed->pattern_sel],
                    PATTERN_NAME_MAX, &ed->edit_pattern_name)) {
        p->name = ed->patterns.names[ed->pattern_sel];
        ed->pattern_dirty = true;
    }
    y += 29;

    const char *origin_names[7];
    for (int i = 0; i < 7; i++) origin_names[i] = OriginName((Origin)i);
    int origin = (int)p->origin;
    if (UICycler("pat_origin", (Rectangle){ x, y, w, 22 }, "Origin", &origin, origin_names, 7)) {
        p->origin = (Origin)origin; pattern_changed(ed);
    }
    y += 27;
    const char *aim_names[11];
    for (int i = 0; i < 11; i++) aim_names[i] = AimName((Aim)i);
    int aim = (int)p->aim;
    if (UICycler("pat_aim", (Rectangle){ x, y, w, 22 }, "Aim", &aim, aim_names, 11)) {
        p->aim = (Aim)aim; pattern_changed(ed);
    }
    y += 27;

    if (p->origin == ORIGIN_POINT) {
        pat_float(ed, "pat_px", x, &y, w, "Point X", &p->point.x, -4000, 5200, "%.0f");
        pat_float(ed, "pat_py", x, &y, w, "Point Y", &p->point.y, -4000, 5200, "%.0f");
    }
    if (p->aim == AIM_WALL || p->aim == AIM_ALTERNATE) {
        const char *side_names[4] = { "top", "bottom", "left", "right" };
        int side = (int)p->side;
        if (UICycler("pat_side", (Rectangle){ x, y, w, 22 }, "Side", &side, side_names, 4)) {
            p->side = (Side)side; pattern_changed(ed);
        }
        y += 27;
        pat_int(ed, "pat_gap", x, &y, w, "Gap", &p->gap, -1, 32, "%d");
    }

    pat_int(ed, "pat_count", x, &y, w, "Count", &p->count, 1, 128, "%d");
    float spread = p->spread * RAD2DEG;
    if (UISliderFloat("pat_spread", (Rectangle){ x, y, w, 22 }, "Spread", &spread, 0, 360, "%.0f deg")) {
        p->spread = spread * DEG2RAD; pattern_changed(ed);
    }
    y += 27;
    pat_float(ed, "pat_speed", x, &y, w, "Speed", &p->speed, 0, 2000, "%.0f px/s");
    pat_float(ed, "pat_delay", x, &y, w, "Delay", &p->delay, 0.02f, 10, "%.2f s");
    pat_int(ed, "pat_repeat", x, &y, w, "Repeat", &p->repeat, 0, 100, "%d");
    pat_int(ed, "pat_damage", x, &y, w, "Damage", &p->damage, 0, 100, "%d");
    pat_float(ed, "pat_radius", x, &y, w, "Radius", &p->radius, 1, 100, "%.1f");
    if (UIColorButton("pat_color", (Rectangle){ x, y, w, 22 }, "Color", &p->color)) pattern_changed(ed);
    y += 28;
    pat_float(ed, "pat_spin", x, &y, w, "Spin", &p->spin, -720, 720, "%.0f");
    pat_float(ed, "pat_jitter", x, &y, w, "Jitter", &p->jitter, 0, 180, "%.0f");

    if (UICheckbox("pat_has_angle", (Rectangle){ x, y, w, 22 }, "Use fixed angle", &p->has_angle))
        pattern_changed(ed);
    y += 27;
    if (p->has_angle) pat_float(ed, "pat_angle", x, &y, w, "Angle", &p->angle, -360, 360, "%.0f deg");
    if (p->aim == AIM_ENCIRCLE || p->aim == AIM_CROSS || p->origin == ORIGIN_PLAYER) {
        pat_float(ed, "pat_spawn_r", x, &y, w, "Spawn radius", &p->spawn_radius, 0, 2000, "%.0f");
        pat_float(ed, "pat_min_r", x, &y, w, "Min radius", &p->min_radius, 0, 2000, "%.0f");
    }
    if (p->aim == AIM_SPEAR || p->aim == AIM_FORMATION) {
        pat_int(ed, "pat_segments", x, &y, w, "Segments", &p->segments, 1, 32, "%d");
        pat_float(ed, "pat_length", x, &y, w, "Length", &p->length, 0, 1000, "%.0f");
    }

    pat_text(ed, "pat_sprite", x, &y, w, "Sprite", p->sprite, SPRITE_NAME_MAX);
    pat_float(ed, "pat_sprite_size", x, &y, w, "Bullet size", &p->sprite_size, 0, 256, "%.0f");
    const char *rot_names[2] = { "upright", "travel" };
    int srot = (int)p->sprite_rot;
    if (UICycler("pat_srot", (Rectangle){ x, y, w, 22 }, "Sprite turn", &srot, rot_names, 2)) {
        p->sprite_rot = (SpriteRot)srot; pattern_changed(ed);
    }
    y += 27;
    pat_float(ed, "pat_gravity", x, &y, w, "Gravity", &p->gravity, 0, 2000, "%.0f");
    pat_text(ed, "pat_script", x, &y, w, "Script", p->script, SCRIPT_NAME_MAX);
    pat_text(ed, "pat_sound", x, &y, w, "Sound", p->sound, SOUND_NAME_MAX);
    pat_float(ed, "pat_warn", x, &y, w, "Warning", &p->warn, -1, 10, "%.2f s");
    pat_float(ed, "pat_warn_r", x, &y, w, "Warn radius", &p->warn_radius, 0, 1000, "%.0f");
    UIPopClip();
}

static bool pattern_filtered(const Editor *ed, int index)
{
    if (!ed->pattern_search[0]) return true;
    const char *a = ed->patterns.names[index];
    const char *needle = ed->pattern_search;
    for (const char *h = a; *h; h++) {
        const char *x = h, *y = needle;
        while (*x && *y && tolower((unsigned char)*x) == tolower((unsigned char)*y)) { x++; y++; }
        if (!*y) return true;
    }
    return false;
}

static void draw_patterns(Editor *ed, Rectangle content)
{
    UITheme *t = UIThemeGet();
    float list_w = fminf(226.0f, content.width * 0.32f);
    Rectangle list = { content.x + 4, content.y + 4, list_w, content.height - 8 };
    DrawRectangleRounded(list, 0.1f, 5, (Color){ 18, 21, 26, 255 });
    DrawRectangleRoundedLinesEx(list, 0.1f, 5, 1.0f, t->border);
    if (UITextField("pat_search", (Rectangle){ list.x + 5, list.y + 5, list.width - 10, 22 },
                    ed->pattern_search, sizeof ed->pattern_search, &ed->pattern_search_editing))
        ed->pattern_list_scroll = 0;

    Rectangle lv = { list.x + 3, list.y + 33, list.width - 6, list.height - 72 };
    int filtered[MAX_PATTERNS];
    int fn = 0;
    for (int i = 0; i < ed->patterns.count; i++)
        if (pattern_filtered(ed, i)) filtered[fn++] = i;
    if (!ed->playing && !ed->pattern_search_editing && !editing_text(ed) && fn > 0 &&
        (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_UP))) {
        int at = 0;
        for (int i = 0; i < fn; i++) if (filtered[i] == ed->pattern_sel) at = i;
        at += IsKeyPressed(KEY_DOWN) ? 1 : -1;
        if (at < 0) at = fn - 1;
        if (at >= fn) at = 0;
        if (filtered[at] != ed->pattern_sel) {
            preview_reset(ed);
            ed->pattern_sel = filtered[at];
            ed->edit_pattern_name = false;
        }
    }
    float scroll = UIScroll("pattern_list", lv, fn * 24.0f + 4, &ed->pattern_list_scroll);
    UIPushClip(lv);
    float y = lv.y + 2 - scroll;
    for (int i = 0; i < fn; i++) {
        int idx = filtered[i];
        bool sel = idx == ed->pattern_sel;
        if (UISelectable(TextFormat("pat_row_%d", idx),
                         (Rectangle){ lv.x + 2, y, lv.width - 10, 23 }, ed->patterns.names[idx],
                         sel, UI_ICON_SCRIPT, sel ? t->accent : t->text_dim)) {
            if (idx != ed->pattern_sel) preview_reset(ed);
            ed->pattern_sel = idx;
            ed->edit_pattern_name = false;
        }
        y += 24;
    }
    UIPopClip();
    if (UIIconButton("pat_add", (Rectangle){ list.x + 5, list.y + list.height - 32, 26, 22 },
                     UI_ICON_PLUS, false, true, "Add pattern")) pattern_add(ed);
    if (UIIconButton("pat_del", (Rectangle){ list.x + 35, list.y + list.height - 32, 26, 22 },
                     UI_ICON_TRASH, false, ed->patterns.count > 1, "Delete pattern")) pattern_delete(ed);
    if (UIButton("pat_save", (Rectangle){ list.x + list.width - 64, list.y + list.height - 32, 58, 22 },
                  ed->pattern_dirty ? "Save *" : "Save", true)) patterns_save_editor(ed);

    float preview_w = fminf(260.0f, content.width * 0.29f);
    Rectangle pv = { list.x + list.width + 8, content.y + 4, preview_w, content.height - 8 };
    pattern_preview_draw(ed, pv);
    UIDrawText("LIVE PREVIEW", pv.x + 8, pv.y + 7, 10, t->accent);
    Rectangle props = { pv.x + pv.width + 8, content.y + 4,
                        content.x + content.width - (pv.x + pv.width + 8) - 4, content.height - 8 };
    draw_pattern_props(ed, props);
}

/* ---------------- levels tab ---------------- */
static void draw_levels(Editor *ed, Rectangle content)
{
    UITheme *t = UIThemeGet();
    float list_w = fminf(220.0f, content.width * 0.30f);
    Rectangle list = { content.x + 4, content.y + 4, list_w, content.height - 8 };
    DrawRectangleRounded(list, 0.1f, 5, (Color){ 18, 21, 26, 255 });
    DrawRectangleRoundedLinesEx(list, 0.1f, 5, 1.0f, t->border);
    UIDrawText("LEVELS", list.x + 8, list.y + 8, 10, t->accent);
    Rectangle lv = { list.x + 3, list.y + 28, list.width - 6, list.height - 66 };
    float scroll = UIScroll("level_list", lv, ed->levels.count * 24.0f + 4, &ed->level_list_scroll);
    UIPushClip(lv);
    float y = lv.y + 2 - scroll;
    for (int i = 0; i < ed->levels.count; i++) {
        if (UISelectable(TextFormat("level_row_%d", i), (Rectangle){ lv.x + 2, y, lv.width - 10, 23 },
                         ed->levels.items[i].name, i == ed->level_sel, UI_ICON_LEVELS,
                         i == ed->level_sel ? t->accent : t->text_dim)) {
            ed->level_sel = i;
            ed->edit_level_name = false;
        }
        y += 24;
    }
    UIPopClip();
    if (UIIconButton("level_add", (Rectangle){ list.x + 5, list.y + list.height - 32, 26, 22 },
                     UI_ICON_PLUS, false, ed->levels.count < LEVEL_MAX_LEVELS, "Add level")) {
        if (ed->levels.count < LEVEL_MAX_LEVELS) {
            Level *l = &ed->levels.items[ed->levels.count];
            memset(l, 0, sizeof *l);
            snprintf(l->name, LEVEL_NAME_MAX, "Level %d", ed->levels.count + 1);
            ed->level_sel = ed->levels.count++;
            ed->levels_dirty = true;
        }
    }
    if (UIIconButton("level_del", (Rectangle){ list.x + 35, list.y + list.height - 32, 26, 22 },
                     UI_ICON_TRASH, false, ed->levels.count > 1, "Delete level")) {
        if (ed->level_sel >= 0 && ed->level_sel < ed->levels.count && ed->levels.count > 1) {
            for (int i = ed->level_sel; i < ed->levels.count - 1; i++)
                ed->levels.items[i] = ed->levels.items[i + 1];
            ed->levels.count--;
            if (ed->level_sel >= ed->levels.count) ed->level_sel = ed->levels.count - 1;
            ed->levels_dirty = true;
        }
    }
    if (UIButton("level_save", (Rectangle){ list.x + list.width - 64, list.y + list.height - 32, 58, 22 },
                  ed->levels_dirty ? "Save *" : "Save", true)) levels_save_editor(ed);

    Rectangle props = { list.x + list.width + 8, content.y + 4,
                        content.x + content.width - (list.x + list.width + 12), content.height - 8 };
    if (ed->levels.count <= 0) {
        UIDrawText("No levels - press + to create one", props.x + 10, props.y + 10, 11, t->text_disabled);
        return;
    }
    if (ed->level_sel < 0) ed->level_sel = 0;
    if (ed->level_sel >= ed->levels.count) ed->level_sel = ed->levels.count - 1;
    Level *lvl = &ed->levels.items[ed->level_sel];
    float content_h = 120.0f + lvl->pattern_count * 25.0f;
    float pscroll = UIScroll("level_props", props, content_h, &ed->level_prop_scroll);
    UIPushClip(props);
    float x = props.x + 10;
    float w = props.width - 24;
    float py = props.y + 5 - pscroll;
    UIDrawText("LEVEL SETTINGS", x, py, 10, t->accent);
    py += 16;
    if (UITextField("level_name", (Rectangle){ x, py, w, 22 }, lvl->name, LEVEL_NAME_MAX,
                    &ed->edit_level_name)) ed->levels_dirty = true;
    py += 30;
    UIDrawText(TextFormat("%d patterns   |   max %d", lvl->pattern_count, LEVEL_MAX_PATTERNS),
               x, py, 11, t->text_dim);
    py += 22;

    for (int i = 0; i < lvl->pattern_count; i++) {
        Rectangle row = { x, py, w - 30, 22 };
        DrawRectangleRounded(row, 0.15f, 4, (Color){ 22, 26, 33, 255 });
        UIDrawText(lvl->patterns[i], row.x + 8, row.y + 5, 11, t->text);
        if (UIIconButton(TextFormat("lvl_del_%d", i), (Rectangle){ x + w - 26, py, 22, 22 },
                         UI_ICON_CLOSE, false, true, "Remove pattern")) {
            for (int k = i; k < lvl->pattern_count - 1; k++)
                memmove(lvl->patterns[k], lvl->patterns[k + 1], LEVEL_PATTERN_MAX);
            lvl->pattern_count--;
            ed->levels_dirty = true;
            break;
        }
        py += 25;
    }

    if (lvl->pattern_count < LEVEL_MAX_PATTERNS && ed->patterns.count > 0) {
        static int add_idx = 0;
        if (add_idx >= ed->patterns.count) add_idx = 0;
        const char *names[MAX_PATTERNS];
        for (int i = 0; i < ed->patterns.count; i++) names[i] = ed->patterns.names[i];
        UICycler("level_add_pattern", (Rectangle){ x, py, w - 72, 22 }, "Pattern", &add_idx,
                 names, ed->patterns.count);
        if (UIButtonAccent("level_add_pat_btn", (Rectangle){ x + w - 66, py, 66, 22 }, "Add", true)) {
            snprintf(lvl->patterns[lvl->pattern_count], LEVEL_PATTERN_MAX, "%s",
                     ed->patterns.names[add_idx]);
            lvl->pattern_count++;
            ed->levels_dirty = true;
        }
        py += 28;
    }
    UIPopClip();
}

static void draw_dock(Editor *ed)
{
    Layout *L = &ed->layout;
    UITheme *t = UIThemeGet();
    Rectangle body = UIPanelBegin(L->dock, NULL);
    float x = body.x + 4;
    const char *tabs[3] = { "CONSOLE", "PATTERNS", "LEVELS" };
    for (int i = 0; i < 3; i++) {
        if (UIFlatButton(TextFormat("dock_tab_%d", i), (Rectangle){ x, body.y + 3, 88, 24 },
                         tabs[i], ed->bottom_tab == i)) ed->bottom_tab = i;
        x += 90;
    }
    if ((ed->bottom_tab == DOCK_PATTERNS && ed->pattern_dirty) ||
        (ed->bottom_tab == DOCK_LEVELS && ed->levels_dirty))
        UIDrawText("unsaved", body.x + body.width - 76, body.y + 9, 10, t->warning);
    if (ed->bottom_tab == DOCK_CONSOLE) {
        if (UIButton("console_clear", (Rectangle){ body.x + body.width - 70, body.y + 4, 64, 22 },
                      "Clear", true)) { ConsoleClear(); ed->console_follow = true; }
    }
    Rectangle content = { body.x, body.y + 30, body.width, body.height - 31 };
    if (ed->bottom_tab == DOCK_CONSOLE) draw_console(ed, content);
    else if (ed->bottom_tab == DOCK_PATTERNS) draw_patterns(ed, content);
    else draw_levels(ed, content);
    UIPanelEnd();
}

/* ---------------- splitters ---------------- */
static void draw_splitters(Editor *ed)
{
    Layout *L = &ed->layout;
    bool hover_l = UIMouseIn(L->left_split);
    bool hover_r = UIMouseIn(L->right_split);
    bool hover_b = UIMouseIn(L->bottom_split);
    if (hover_l || hover_r || ed->split_drag == 1 || ed->split_drag == 2)
        SetMouseCursor(MOUSE_CURSOR_RESIZE_EW);
    else if (hover_b || ed->split_drag == 3)
        SetMouseCursor(MOUSE_CURSOR_RESIZE_NS);
    else
        SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    if (hover_l && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ed->split_drag = 1;
    if (hover_r && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ed->split_drag = 2;
    if (hover_b && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ed->split_drag = 3;
    if (ed->split_drag && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 d = GetMouseDelta();
        if (ed->split_drag == 1) ed->left_w = clampf(ed->left_w + d.x, 180, 380);
        if (ed->split_drag == 2) ed->right_w = clampf(ed->right_w - d.x, 240, 460);
        if (ed->split_drag == 3) ed->bottom_h = clampf(ed->bottom_h - d.y, 140, 620);
        ed->layout = layout_compute(ed->win_w, ed->win_h, ed->left_w, ed->right_w, ed->bottom_h);
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) ed->split_drag = 0;
}

static void draw_about(Editor *ed)
{
    if (!ed->show_about) return;
    UIBlockPointer(false);   /* the modal itself stays clickable */
    UITheme *t = UIThemeGet();
    DrawRectangle(0, 0, ed->win_w, ed->win_h, Fade(BLACK, 0.55f));
    Rectangle box = { ed->win_w * 0.5f - 250, ed->win_h * 0.5f - 165, 500, 330 };
    UIShadow(box, 0.04f, 8);
    DrawRectangleRounded(box, 0.04f, 10, t->panel);
    DrawRectangleRoundedLinesEx(box, 0.04f, 10, 1.0f, t->border_light);
    Rectangle head = { box.x, box.y, box.width, 52 };
    DrawRectangleRounded(head, 0.08f, 8, t->header);
    UIDrawText("BHCPP ENGINE", box.x + 22, box.y + 12, 19, t->text);
    UIDrawText(TextFormat("version %s", EngineVersion()), box.x + 22, box.y + 33, 11, t->text_dim);
    float y = box.y + 72;
    UIDrawText("A small C99 game engine and bullet-hell editor.", box.x + 24, y, 13, t->text); y += 24;
    UIDrawText("Engine modules: core, input, console, assets, scene graph,", box.x + 24, y, 12, t->text_dim); y += 18;
    UIDrawText("level format, immediate-mode UI and editor shell.", box.x + 24, y, 12, t->text_dim); y += 30;
    UIDrawText("The game is a separate module: it owns the run, menus and scoring.", box.x + 24, y, 12, t->text_dim); y += 18;
    UIDrawText("Play mode renders it into the viewport at 1200x600.", box.x + 24, y, 12, t->text_dim); y += 34;
    UIDrawText("raylib 6.2  |  Lua 5.4.7  |  MinGW-w64", box.x + 24, y, 11, t->text_disabled);
    if (UIButtonAccent("about_close", (Rectangle){ box.x + box.width - 110, box.y + box.height - 42, 88, 26 },
                       "Close", true)) ed->show_about = false;
    if (IsKeyPressed(KEY_ESCAPE)) ed->show_about = false;
}

static void draw_editor(Editor *ed)
{
    UITheme *t = UIThemeGet();
    if (GetScreenWidth() != ed->win_w || GetScreenHeight() != ed->win_h) layout_apply(ed);
    UIBeginFrame(GetMousePosition());
    if (ed->show_about) UIBlockPointer(true);
    DrawRectangle(0, 0, ed->win_w, ed->win_h, t->background);
    draw_menu_bar(ed);
    draw_toolbar(ed);
    draw_left(ed);
    draw_viewport(ed);
    draw_inspector(ed);
    draw_dock(ed);
    draw_splitters(ed);
    draw_status(ed);
    draw_dropdown(ed);
    draw_about(ed);
    UIEndFrame();
}

static Editor *editor_create(void)
{
    Editor *ed = (Editor *)calloc(1, sizeof(Editor));
    if (!ed) return NULL;
    ed->left_w = 262;
    ed->right_w = 318;
    ed->bottom_h = 258;
    ed->tool = TOOL_MOVE;
    ed->show_grid = true;
    ed->snap = true;
    ed->snap_size = 10;
    ed->cam_center = (Vector2){ BASE_W * 0.5f, BASE_H * 0.5f };
    ed->cam_zoom = 1;
    ed->bottom_tab = DOCK_CONSOLE;
    ed->running = true;
    ed->console_follow = true;
    ed->console_history_pos = 0;
    ed->pattern_sel = 0;
    ed->level_sel = 0;
    ed->selected = -1;
    scene_init_path(ed);
    patterns_load_editor(ed);
    levels_load_editor(ed);
    assets_scan_editor(ed);
    menus_init(ed);
    preview_reset(ed);
    ConsolePrint(CONSOLE_OUTPUT, "%s %s ready", ENGINE_NAME, EngineVersion());
    ConsolePrint(CONSOLE_INFO, "Scene: %s", ed->scene_path);
    return ed;
}

static void editor_destroy(Editor *ed)
{
    if (!ed) return;
    play_stop(ed);
    free(ed);
}

static void cmd_clear(int argc, char **argv) { (void)argc; (void)argv; ConsoleClear(); }
static void cmd_version(int argc, char **argv) { (void)argc; (void)argv; ConsolePrint(CONSOLE_OUTPUT, "%s %s", ENGINE_NAME, ENGINE_VERSION); }
static void cmd_scene(int argc, char **argv)
{
    if (!g_editor) return;
    if (argc > 1 && strcmp(argv[1], "save") == 0) { scene_save(g_editor); return; }
    if (argc > 1 && strcmp(argv[1], "new") == 0) { scene_new(g_editor); return; }
    ConsolePrint(CONSOLE_OUTPUT, "scene '%s'  %d entities  %s", g_editor->scene.name,
                 g_editor->scene.count, g_editor->scene_dirty ? "(unsaved)" : "(saved)");
    ConsolePrint(CONSOLE_OUTPUT, "  %s", g_editor->scene_path);
}
static void cmd_assets(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!g_editor) return;
    ConsolePrint(CONSOLE_OUTPUT, "assets: %d textures, %d scripts, %d data, %d scenes",
                 g_editor->textures.count, g_editor->scripts.count,
                 g_editor->data.count, g_editor->scene_files.count);
    if (argc > 1) assets_scan_editor(g_editor);
}
static void cmd_patterns(int argc, char **argv)
{
    if (!g_editor) return;
    if (argc > 1 && strcmp(argv[1], "save") == 0) { patterns_save_editor(g_editor); return; }
    ConsolePrint(CONSOLE_OUTPUT, "patterns: %d loaded  %s", g_editor->patterns.count,
                 g_editor->pattern_dirty ? "(unsaved)" : "(saved)");
}
static void cmd_levels(int argc, char **argv)
{
    if (!g_editor) return;
    if (argc > 1 && strcmp(argv[1], "save") == 0) { levels_save_editor(g_editor); return; }
    ConsolePrint(CONSOLE_OUTPUT, "levels: %d  %s", g_editor->levels.count,
                 g_editor->levels_dirty ? "(unsaved)" : "(saved)");
}
static void cmd_play(int argc, char **argv) { (void)argc; (void)argv; if (g_editor) play_start(g_editor); }
static void cmd_stop(int argc, char **argv) { (void)argc; (void)argv; if (g_editor) play_stop(g_editor); }
static void cmd_pause(int argc, char **argv) { (void)argc; (void)argv; if (g_editor) play_toggle_pause(g_editor); }
static void cmd_step(int argc, char **argv) { (void)argc; (void)argv; if (g_editor) play_step(g_editor); }
static void cmd_reload(int argc, char **argv) { (void)argc; (void)argv; if (g_editor) reload_all(g_editor); }
static void cmd_about(int argc, char **argv) { (void)argc; (void)argv; if (g_editor) g_editor->show_about = true; }

static void register_editor_commands(void)
{
    ConsoleRegister("clear", "empty the console", cmd_clear);
    ConsoleRegister("version", "engine version", cmd_version);
    ConsoleRegister("scene", "scene [save|new] - scene status", cmd_scene);
    ConsoleRegister("assets", "asset counts; 'assets scan' rescans", cmd_assets);
    ConsoleRegister("patterns", "patterns [save] - pattern table", cmd_patterns);
    ConsoleRegister("levels", "levels [save] - level set", cmd_levels);
    ConsoleRegister("play", "start play-in-editor", cmd_play);
    ConsoleRegister("stop", "stop play-in-editor", cmd_stop);
    ConsoleRegister("pause", "toggle play pause", cmd_pause);
    ConsoleRegister("step", "advance one game frame", cmd_step);
    ConsoleRegister("reload", "reload scene, patterns, levels and assets", cmd_reload);
    ConsoleRegister("about", "show the about dialog", cmd_about);
}
static int editor_check_patterns(void)
{
    PatternSet set;
    memset(&set, 0, sizeof set);
    char err[256] = { 0 };
    int n = PatternsLoad("resources/patterns.txt", &set, err, sizeof err);
    if (n < 0) {
        printf("FAIL: patterns.txt: %s\n", err);
        return 1;
    }
    if (n != PATTERN_COUNT) {
        printf("FAIL: patterns.txt has %d patterns, built-in table has %d\n", n, PATTERN_COUNT);
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (strcmp(set.items[i].name, PATTERNS[i].name) != 0) {
            printf("FAIL: pattern %d is '%s', expected '%s'\n", i, set.items[i].name, PATTERNS[i].name);
            return 1;
        }
    }
    printf("OK: patterns.txt matches the built-in table (%d patterns)\n", n);

    Scene scene;
    SceneInit(&scene, "Check");
    if (!SceneLoad(&scene, "resources/scenes/main.scene", err, sizeof err)) {
        printf("FAIL: scenes/main.scene: %s\n", err);
        return 1;
    }
    if (SceneFindTag(&scene, "player_spawn") < 0) {
        printf("FAIL: scenes/main.scene has no player_spawn tag\n");
        return 1;
    }
    printf("OK: main.scene loads (%d entities, player spawn found)\n", scene.count);

    LevelSet levels;
    LevelsClear(&levels);
    if (!LevelsLoad("resources/levels.txt", &levels, err, sizeof err) || levels.count <= 0) {
        printf("FAIL: levels.txt: %s\n", err[0] ? err : "no levels");
        return 1;
    }
    for (int i = 0; i < levels.count; i++) {
        Level *l = &levels.items[i];
        for (int k = 0; k < l->pattern_count; k++) {
            bool found = false;
            for (int p = 0; p < n; p++) if (strcmp(l->patterns[k], set.items[p].name) == 0) found = true;
            if (!found) {
                printf("FAIL: level '%s' references missing pattern '%s'\n", l->name, l->patterns[k]);
                return 1;
            }
        }
    }
    printf("OK: levels.txt loads (%d levels)\n", levels.count);
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            printf("BHCPP editor\n");
            printf("  editor.exe             open the editor\n");
            printf("  editor.exe --layout    verify dock layout, no window\n");
            printf("  editor.exe --check     validate resources/patterns.txt\n");
            return 0;
        }
        if (strcmp(argv[i], "--layout") == 0) return layout_check();
        if (strcmp(argv[i], "--check") == 0) return editor_check_patterns();
    }

    EngineConfig cfg = EngineDefaultConfig();
    cfg.width = WIN_W;
    cfg.height = WIN_H;
    cfg.min_width = 1100;
    cfg.min_height = 700;
    cfg.title = "BHCPP Editor";
    cfg.resizable = true;
    cfg.vsync = true;
    cfg.target_fps = 60;
    if (!EngineInit(&cfg)) return 1;
    SetExitKey(KEY_NULL);
    UIInit();
    EngineSetLogSink(ConsoleLogSink);

    Editor *ed = editor_create();
    if (!ed) {
        EngineLogError("editor: out of memory");
        UIShutdown();
        EngineShutdown();
        return 1;
    }
    g_editor = ed;
    register_editor_commands();
    layout_apply(ed);

    while (ed->running) {
        if (!EngineBeginFrame()) break;
        if (EngineShouldClose()) ed->running = false;
        editor_update(ed, EngineDeltaTime());
        BeginDrawing();
        ClearBackground((Color){ 15, 17, 21, 255 });
        draw_editor(ed);
        EndDrawing();
        EngineEndFrame();
    }

    g_editor = NULL;
    editor_destroy(ed);
    EngineSetLogSink(NULL);
    UIShutdown();
    EngineShutdown();
    return 0;
}

