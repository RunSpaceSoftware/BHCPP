/* ==========================================================================
 *  tools/pattern_editor.c  --  a small companion tool for Bullet Hell
 *
 *  Lists every bullet pattern, shows a live preview of it, and lets you edit
 *  every field with sliders / cyclers. Saves straight to
 *  resources/patterns.txt, the file the game reads at start-up, so there is
 *  no rebuild step: edit, save, run the game.
 *
 *  Build:  mingw32-make editor
 *  Run:    tools\pattern_editor.exe            (window)
 *          tools\pattern_editor.exe --check    (verify the file against the
 *                                               built-in table in patterns.c)
 *          tools\pattern_editor.exe --export resources/patterns.txt
 *                                              (write the built-in table out)
 *
 *  Controls (window):
 *    click a pattern in the list ....... select
 *    mouse in the arena ................ aims "player"-style patterns
 *    drag / wheel on a slider .......... change the value
 *    click < or > on a row ............. cycle an enum
 *    Space ............................. pause / resume the preview
 *    R ................................. clear the bullets and restart
 *    Ctrl+S ............................ save
 *    Ctrl+R ............................ reload from disk
 *    Up / Down ......................... previous / next pattern
 * ========================================================================== */

#include "raylib.h"
#include "bullet.h"
#include "patterns.h"
#include "patterns_io.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN_W 1500
#define WIN_H 820
#define ARENA_W 1200
#define ARENA_H 600

#define CANVAS_X 292
#define CANVAS_Y 56
#define CANVAS_SCALE 0.7f
#define CANVAS_W ((int)(ARENA_W * CANVAS_SCALE))
#define CANVAS_H ((int)(ARENA_H * CANVAS_SCALE))

#define LIST_X 10
#define LIST_Y 96
#define LIST_W 270
#define LIST_ROW_H 30
#define LIST_VISIBLE 20

#define PANEL_X 1150
#define PANEL_Y 56
#define PANEL_W 340
#define PROP_ROW_H 32

static const Color C_BG      = { 18, 18, 24, 255 };
static const Color C_PANEL   = { 28, 28, 38, 255 };
static const Color C_ROW     = { 34, 34, 46, 255 };
static const Color C_TEXT    = { 220, 220, 230, 255 };
static const Color C_DIM     = { 130, 130, 150, 255 };
static const Color C_ACCENT  = { 255, 200, 90, 255 };
static const Color C_SELECT  = { 70, 60, 30, 255 };
static const Color C_BORDER  = { 60, 60, 78, 255 };

/* ---------------------------------------------------------------- state */
static PatternSet set;
static int         count = 0;
static int         sel = 0;
static int         list_offset = 0;
static char        path[4096] = { 0 };

static RenderTexture2D canvas;

static BulletList bullets;
static SpinState  spin;
static float      shot_timer = 0.0f;
static int        shots_fired = 0;
static bool       paused = false;
static float      player_x = ARENA_W / 2.0f;
static float      player_y = ARENA_H / 2.0f;

static int   drag_id = -1;
static char  status[512] = { 0 };
static float status_time = -10.0f;

static bool editing_name = false;
static char name_buf[PATTERN_NAME_MAX] = { 0 };

/* pngs found in the resources folder, entry 0 = "" (draw a circle) */
#define MAX_SPRITE_CHOICES 32
static char        sprite_choices[MAX_SPRITE_CHOICES][SPRITE_NAME_MAX];
static const char *sprite_labels[MAX_SPRITE_CHOICES];
static int         sprite_choice_count = 1;

/* --------------------------------------------------------------- helpers */
static void say(const char *text)
{
    snprintf(status, sizeof status, "%s", text);
    status_time = GetTime();
}

static void fix_names(void)
{
    for (int i = 0; i < count; i++) set.items[i].name = set.names[i];
}

static Pattern default_pattern(void)
{
    Pattern p;
    memset(&p, 0, sizeof p);
    p.origin = ORIGIN_EDGE;
    p.aim = AIM_PLAYER;
    p.side = SIDE_TOP;
    p.count = 1;
    p.speed = 300;
    p.delay = 0.3f;
    p.repeat = 10;
    return p;
}

static void reset_preview(void)
{
    bullets.count = 0;
    spin.angle = 0.0f;
    shot_timer = 0.0f;
    shots_fired = 0;
}

static void select_pattern(int index)
{
    if (index < 0 || index >= count || index == sel) return;
    sel = index;
    if (sel < list_offset) list_offset = sel;
    if (sel >= list_offset + LIST_VISIBLE) list_offset = sel - LIST_VISIBLE + 1;
    reset_preview();
}

/* Bullet sprites available to pick from: every png next to patterns.txt.
   Entry 0 stays the empty name so a pattern can go back to a plain circle. */
static void scan_sprites(void)
{
    sprite_choice_count = 1;
    snprintf(sprite_choices[0], SPRITE_NAME_MAX, "%s", "");
    sprite_labels[0] = "(circle)";

    char dir[4096];
    snprintf(dir, sizeof dir, "%s", GetDirectoryPath(path));
    FilePathList files = LoadDirectoryFilesEx(dir, ".png", false);
    for (unsigned i = 0; i < files.count && sprite_choice_count < MAX_SPRITE_CHOICES; i++) {
        const char *base = GetFileNameWithoutExt(files.paths[i]);  /* static buffer: copy now */
        if (!base || !*base) continue;
        if (strcmp(base, "player") == 0) continue;   /* the ship, not a bullet */
        snprintf(sprite_choices[sprite_choice_count], SPRITE_NAME_MAX, "%s", base);
        sprite_labels[sprite_choice_count] = sprite_choices[sprite_choice_count];
        sprite_choice_count++;
    }
    UnloadDirectoryFiles(files);
}

/* Index of `name` in the sprite list; an unknown name is appended so editing a
   hand-written pattern never silently loses its sprite. */
static int sprite_index(const char *name)
{
    if (!name || !*name) return 0;
    for (int i = 1; i < sprite_choice_count; i++)
        if (strcmp(sprite_choices[i], name) == 0) return i;
    if (sprite_choice_count < MAX_SPRITE_CHOICES) {
        snprintf(sprite_choices[sprite_choice_count], SPRITE_NAME_MAX, "%s", name);
        sprite_labels[sprite_choice_count] = sprite_choices[sprite_choice_count];
        return sprite_choice_count++;
    }
    return 0;
}

/* ------------------------------------------------------------ file paths */
static void resolve_path(const char *arg)
{
    if (arg && *arg) {
        snprintf(path, sizeof path, "%s", arg);
        return;
    }

    if (PatternsPath()[0]) {
        snprintf(path, sizeof path, "%s", PatternsPath());
        return;
    }

    /* no patterns.txt yet: create it next to the resources folder */
    const char *app = GetApplicationDirectory();
    char dir[4096];
    snprintf(dir, sizeof dir, "%s../resources", app);
    if (DirectoryExists(dir)) snprintf(path, sizeof path, "%s../resources/patterns.txt", app);
    else snprintf(path, sizeof path, "%sresources/patterns.txt", app);
}

/* Load the pattern list: the file given on the command line when there is
   one, otherwise whatever PatternsGet() finds (resources/patterns.txt or the
   built-in table), and only then decide where Save will write. */
static void load_initial(const char *arg)
{
    int n = -1;
    char err[256] = { 0 };

    if (arg && *arg) {
        n = PatternsLoad(arg, &set, err, sizeof err);
        if (n < 0) {
            say(err);
            n = -1;
        }
    }

    if (n < 0) {
        const Pattern *src = PatternsGet(&n);
        if (n > MAX_PATTERNS) n = MAX_PATTERNS;
        for (int i = 0; i < n; i++) {
            set.items[i] = src[i];
            snprintf(set.names[i], PATTERN_NAME_MAX, "%s", (src[i].name && *src[i].name) ? src[i].name : "unnamed");
            set.names[i][PATTERN_NAME_MAX - 1] = '\0';
        }
    }

    count = n;
    fix_names();
    resolve_path(arg);
    scan_sprites();
    sel = 0;
    reset_preview();
}

static void do_save(void)
{
    if (PatternsSave(path, set.items, count)) {
        char msg[4200];
        snprintf(msg, sizeof msg, "saved %d patterns to %s", count, path);
        say(msg);
    } else {
        char msg[4200];
        snprintf(msg, sizeof msg, "could not write %s", path);
        say(msg);
    }
}

static void do_reload(void)
{
    PatternSet tmp;
    char err[256] = { 0 };
    int n = PatternsLoad(path, &tmp, err, sizeof err);
    if (n > 0) {
        set = tmp;
        count = n > MAX_PATTERNS ? MAX_PATTERNS : n;
        fix_names();
        if (sel >= count) sel = count - 1;
        reset_preview();
        scan_sprites();              /* picks up pngs dropped in since start-up */
        reload_bullet_sprites();     /* reset_preview() cleared the bullets above */
        say("reloaded from disk");
    } else {
        say(err);
    }
}

/* --------------------------------------------------------------- widgets */
static Rectangle prop_row(int index)
{
    return (Rectangle){ (float)PANEL_X, (float)(PANEL_Y + index * PROP_ROW_H), (float)PANEL_W, (float)PROP_ROW_H };
}

static bool mouse_on(Rectangle r)
{
    return CheckCollisionPointRec(GetMousePosition(), r);
}

static void draw_row_text(Rectangle r, const char *label, const char *value)
{
    DrawText(label, (int)r.x, (int)(r.y + 7), 17, C_TEXT);
    if (value && *value) {
        int w = MeasureText(value, 17);
        DrawText(value, (int)(r.x + r.width - w), (int)(r.y + 7), 17, C_ACCENT);
    }
}

/* Returns true when the value changed. */
static bool slider_f(const char *label, int index, float *value, float lo, float hi, float step)
{
    Rectangle r = prop_row(index);
    Rectangle bar = { r.x + 122, r.y + 14, r.width - 196, 9 };

    bool changed = false;
    Vector2 m = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(bar)) drag_id = index;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && drag_id == index) drag_id = -1;

    if (drag_id == index && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float t = (m.x - bar.x) / bar.width;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        *value = lo + t * (hi - lo);
        changed = true;
    }

    if (mouse_on(r) && GetMouseWheelMove() != 0.0f) {
        *value += GetMouseWheelMove() * step;
        changed = true;
    }

    if (*value < lo) *value = lo;
    if (*value > hi) *value = hi;

    draw_row_text(r, label, TextFormat("%.2f", *value));

    DrawRectangleRec(bar, C_ROW);
    float t = (hi > lo) ? (*value - lo) / (hi - lo) : 0.0f;
    DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * t), (int)bar.height, C_ACCENT);
    DrawRectangleLinesEx(bar, 1.0f, C_BORDER);
    DrawCircleV((Vector2){ bar.x + bar.width * t, bar.y + bar.height / 2.0f }, 5.0f, C_TEXT);

    return changed;
}

static bool slider_i(const char *label, int index, int *value, int lo, int hi)
{
    float v = (float)*value;
    Rectangle r = prop_row(index);
    Rectangle bar = { r.x + 122, r.y + 14, r.width - 196, 9 };

    bool changed = false;
    Vector2 m = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(bar)) drag_id = index;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && drag_id == index) drag_id = -1;

    if (drag_id == index && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float t = (m.x - bar.x) / bar.width;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        v = lo + t * (hi - lo);
        changed = true;
    }

    if (mouse_on(r) && GetMouseWheelMove() != 0.0f) {
        v += GetMouseWheelMove();
        changed = true;
    }

    if (v < lo) v = lo;
    if (v > hi) v = hi;
    int iv = (int)lroundf(v);
    if (iv != *value) changed = true;
    *value = iv;

    draw_row_text(r, label, TextFormat("%d", *value));

    DrawRectangleRec(bar, C_ROW);
    float t = (hi > lo) ? ((float)*value - lo) / (hi - lo) : 0.0f;
    DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * t), (int)bar.height, C_ACCENT);
    DrawRectangleLinesEx(bar, 1.0f, C_BORDER);
    DrawCircleV((Vector2){ bar.x + bar.width * t, bar.y + bar.height / 2.0f }, 5.0f, C_TEXT);

    return changed;
}

static const char *const ORIGIN_LABELS[] = { "edge", "top", "bottom", "left", "right", "point", "player" };
static const char *const AIM_LABELS[] = { "player", "angle", "radial", "wall", "rain", "crossfire", "encircle", "cross", "spear", "formation" };
#define AIM_LABEL_COUNT 10
static const char *const SIDE_LABELS[] = { "top", "bottom", "left", "right" };

/* Cyclic choice with clickable < > zones. */
static bool cycler(const char *label, int index, int *value, const char *const *names, int n)
{
    Rectangle r = prop_row(index);
    Rectangle left = { r.x + 122, r.y + 5, 24, 22 };
    Rectangle right = { r.x + r.width - 34, r.y + 5, 24, 22 };

    bool changed = false;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(left)) {
        *value = (*value + n - 1) % n;
        changed = true;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(right)) {
        *value = (*value + 1) % n;
        changed = true;
    }

    DrawText(label, (int)r.x, (int)(r.y + 7), 17, C_TEXT);

    const char *text = (*value >= 0 && *value < n) ? names[*value] : "?";
    int tw = MeasureText(text, 17);
    DrawText(text, (int)(r.x + 122 + (r.width - 196 - tw) / 2), (int)(r.y + 7), 17, C_ACCENT);

    DrawRectangleRec(left, mouse_on(left) ? (Color){ 70, 70, 90, 255 } : C_ROW);
    DrawRectangleRec(right, mouse_on(right) ? (Color){ 70, 70, 90, 255 } : C_ROW);
    DrawText("<", (int)left.x + 8, (int)left.y + 3, 17, C_TEXT);
    DrawText(">", (int)right.x + 8, (int)right.y + 3, 17, C_TEXT);

    return changed;
}

/* Gap: 0 = no gap, GAP_RANDOM = random slot, n>0 = leave slot n-1 empty. */
static int gap_next(int g)
{
    if (g == 0) return GAP_RANDOM;
    if (g == GAP_RANDOM) return GAP_AT(0);
    if (g >= GAP_AT(23)) return 0;
    return g + 1;
}

static int gap_prev(int g)
{
    if (g == 0) return GAP_AT(23);
    if (g == GAP_RANDOM) return 0;
    if (g <= GAP_AT(0)) return GAP_RANDOM;
    return g - 1;
}

static bool gap_widget(const char *label, int index, int *gap)
{
    Rectangle r = prop_row(index);
    Rectangle left = { r.x + 122, r.y + 5, 24, 22 };
    Rectangle right = { r.x + r.width - 34, r.y + 5, 24, 22 };

    bool changed = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(left)) { *gap = gap_prev(*gap); changed = true; }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(right)) { *gap = gap_next(*gap); changed = true; }

    DrawText(label, (int)r.x, (int)(r.y + 7), 17, C_TEXT);

    char text[32];
    if (*gap == 0) snprintf(text, sizeof text, "none");
    else if (*gap < 0) snprintf(text, sizeof text, "random");
    else snprintf(text, sizeof text, "slot %d", *gap - 1);

    int tw = MeasureText(text, 17);
    DrawText(text, (int)(r.x + 122 + (r.width - 196 - tw) / 2), (int)(r.y + 7), 17, C_ACCENT);

    DrawRectangleRec(left, mouse_on(left) ? (Color){ 70, 70, 90, 255 } : C_ROW);
    DrawRectangleRec(right, mouse_on(right) ? (Color){ 70, 70, 90, 255 } : C_ROW);
    DrawText("<", (int)left.x + 8, (int)left.y + 3, 17, C_TEXT);
    DrawText(">", (int)right.x + 8, (int)right.y + 3, 17, C_TEXT);

    return changed;
}

static bool checkbox(const char *label, int index, bool *value)
{
    Rectangle r = prop_row(index);
    Rectangle box = { r.x + 122, r.y + 7, 18, 18 };

    bool changed = false;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(r)) {
        *value = !*value;
        changed = true;
    }

    DrawText(label, (int)r.x, (int)(r.y + 7), 17, C_TEXT);
    DrawRectangleRec(box, *value ? C_ACCENT : C_ROW);
    DrawRectangleLinesEx(box, 1.0f, C_BORDER);
    if (*value) DrawText("x", (int)box.x + 5, (int)box.y + 1, 17, (Color){ 20, 20, 20, 255 });
    return changed;
}

static bool button(Rectangle r, const char *text, bool enabled)
{
    bool hover = enabled && mouse_on(r);
    DrawRectangleRec(r, hover ? (Color){ 80, 80, 104, 255 } : (enabled ? C_ROW : (Color){ 30, 30, 38, 255 }));
    DrawRectangleLinesEx(r, 1.0f, C_BORDER);
    int tw = MeasureText(text, 17);
    DrawText(text, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - 17) / 2), 17,
             enabled ? C_TEXT : C_DIM);
    return enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* ------------------------------------------------------------- preview */
static void draw_canvas(void)
{
    BeginTextureMode(canvas);
    ClearBackground((Color){ 8, 8, 12, 255 });

    for (int x = 100; x < ARENA_W; x += 100) DrawLine(x, 0, x, ARENA_H, (Color){ 18, 18, 26, 255 });
    for (int y = 100; y < ARENA_H; y += 100) DrawLine(0, y, ARENA_W, y, (Color){ 18, 18, 26, 255 });

    draw_bullets(&bullets);

    /* player marker */
    DrawCircleV((Vector2){ player_x, player_y }, 10.0f, (Color){ 80, 160, 255, 110 });
    DrawCircleLinesV((Vector2){ player_x, player_y }, 10.0f, (Color){ 120, 200, 255, 255 });

    if (paused) DrawText("PAUSED", 12, ARENA_H - 30, 20, (Color){ 255, 120, 120, 255 });

    DrawRectangleLines(0, 0, ARENA_W, ARENA_H, (Color){ 50, 50, 70, 255 });
    EndTextureMode();

    DrawTexturePro(canvas.texture,
                   (Rectangle){ 0, 0, (float)ARENA_W, (float)-ARENA_H },
                   (Rectangle){ (float)CANVAS_X, (float)CANVAS_Y, (float)CANVAS_W, (float)CANVAS_H },
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
    DrawRectangleLinesEx((Rectangle){ (float)CANVAS_X, (float)CANVAS_Y, (float)CANVAS_W, (float)CANVAS_H }, 1.0f, C_BORDER);
}

/* --------------------------------------------------------------- panels */
static void draw_list(void)
{
    DrawText("PATTERNS", LIST_X, 60, 20, C_DIM);
    char cnt[32];
    snprintf(cnt, sizeof cnt, "%d", count);
    DrawText(cnt, LIST_X + LIST_W - MeasureText(cnt, 20), 60, 20, C_DIM);

    if (mouse_on((Rectangle){ LIST_X, LIST_Y, LIST_W, LIST_VISIBLE * (float)LIST_ROW_H })) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            list_offset -= (int)wheel;
            int max_offset = count > LIST_VISIBLE ? count - LIST_VISIBLE : 0;
            if (list_offset < 0) list_offset = 0;
            if (list_offset > max_offset) list_offset = max_offset;
        }
    }

    for (int i = 0; i < LIST_VISIBLE; i++) {
        int index = list_offset + i;
        if (index >= count) break;

        Rectangle r = { (float)LIST_X, (float)(LIST_Y + i * LIST_ROW_H), (float)LIST_W, (float)(LIST_ROW_H - 2) };
        bool selected = (index == sel);

        DrawRectangleRec(r, selected ? C_SELECT : (mouse_on(r) ? C_ROW : (Color){ 24, 24, 32, 255 }));
        if (selected) DrawRectangle((int)r.x, (int)r.y, 3, (int)r.height, C_ACCENT);
        DrawText(set.items[index].name, (int)r.x + 10, (int)(r.y + 6), 16, selected ? C_ACCENT : C_TEXT);

        if (mouse_on(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) select_pattern(index);
    }

    float by = LIST_Y + LIST_VISIBLE * (float)LIST_ROW_H + 8;
    if (button((Rectangle){ LIST_X, by, 92, 30 }, "+ New", count < MAX_PATTERNS)) {
        if (count < MAX_PATTERNS) {
            for (int i = count; i > sel + 1; i--) {
                set.items[i] = set.items[i - 1];
                memcpy(set.names[i], set.names[i - 1], PATTERN_NAME_MAX);
            }
            set.items[sel + 1] = default_pattern();
            snprintf(set.names[sel + 1], PATTERN_NAME_MAX, "new_pattern_%d", count + 1);
            count++;
            fix_names();
            select_pattern(sel + 1);
            say("added a pattern");
        }
    }
    if (button((Rectangle){ LIST_X + 97, by, 92, 30 }, "Duplicate", count < MAX_PATTERNS && count > 0)) {
        if (count < MAX_PATTERNS) {
            for (int i = count; i > sel + 1; i--) {
                set.items[i] = set.items[i - 1];
                memcpy(set.names[i], set.names[i - 1], PATTERN_NAME_MAX);
            }
            set.items[sel + 1] = set.items[sel];
            char copy_name[PATTERN_NAME_MAX];
            snprintf(copy_name, sizeof copy_name, "%.40s_copy", set.names[sel]);
            memcpy(set.names[sel + 1], copy_name, sizeof copy_name);
            count++;
            fix_names();
            select_pattern(sel + 1);
            say("duplicated the pattern");
        }
    }
    if (button((Rectangle){ LIST_X + 194, by, 92, 30 }, "Delete", count > 1)) {
        if (count > 1) {
            for (int i = sel; i < count - 1; i++) {
                set.items[i] = set.items[i + 1];
                memcpy(set.names[i], set.names[i + 1], PATTERN_NAME_MAX);
            }
            count--;
            if (sel >= count) sel = count - 1;
            fix_names();
            reset_preview();
            say("deleted the pattern");
        }
    }

    if (button((Rectangle){ LIST_X, by + 38, 145, 30 }, "Save  (Ctrl+S)", true)) do_save();
    if (button((Rectangle){ LIST_X + 151, by + 38, 135, 30 }, "Reload (Ctrl+R)", true)) do_reload();
}

static void draw_info(void)
{
    Rectangle name_box = { CANVAS_X, CANVAS_Y + CANVAS_H + 14, 470, 34 };

    if (!editing_name && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse_on(name_box)) {
        snprintf(name_buf, sizeof name_buf, "%s", set.items[sel].name);
        editing_name = true;
    }

    if (editing_name) {
        DrawRectangleRec(name_box, C_ROW);
        DrawRectangleLinesEx(name_box, 1.0f, C_ACCENT);
        DrawText(name_buf, (int)name_box.x + 8, (int)name_box.y + 8, 19, C_TEXT);
        if (((int)(GetTime() * 2)) % 2 == 0) {
            int w = MeasureText(name_buf, 19);
            DrawRectangle((int)name_box.x + 8 + w, (int)name_box.y + 8, 2, 19, C_ACCENT);
        }
        DrawText("Enter to confirm", (int)name_box.x, (int)name_box.y + 40, 15, C_DIM);
    } else {
        DrawRectangleRec(name_box, C_PANEL);
        DrawText(set.items[sel].name, (int)name_box.x + 8, (int)name_box.y + 8, 19, C_TEXT);
        DrawText("click to rename", (int)name_box.x + name_box.width - 116, (int)name_box.y + 10, 14, C_DIM);
    }

    /* colour swatch */
    Rectangle sw = { name_box.x + name_box.width + 16, name_box.y, 54, 34 };
    Color c = set.items[sel].color;
    c.a = 255;
    DrawRectangleRec(sw, c);
    DrawRectangleLinesEx(sw, 1.0f, C_BORDER);
    DrawText(TextFormat("%d,%d,%d", c.r, c.g, c.b), (int)sw.x + 62, (int)sw.y + 8, 15, C_DIM);

    DrawText("Enter: start pattern   |   bullets leave the arena and are recycled",
             CANVAS_X, (int)name_box.y + 48, 15, C_DIM);

    if (status[0] && GetTime() - status_time < 6.0) {
        float a = 1.0f;
        float age = (float)(GetTime() - status_time);
        if (age > 5.0f) a = 6.0f - age;
        Color sc = C_DIM;
        sc.a = (unsigned char)(255 * a);
        DrawText(status, CANVAS_X, (int)name_box.y + 76, 15, sc);
    }

    DrawText("Space: pause    R: restart    Up/Down: previous / next",
             CANVAS_X, WIN_H - 30, 15, C_DIM);
}

/* Which rows the panel shows. Kept as predicates rather than inline tests so
   the --layout check below counts exactly the rows draw_panel() will draw.
   Only relevant rows are shown: the panel would otherwise run past the
   bottom of the window, and a wall's "gap" sitting next to a spear's
   "segments" only invites wrong edits. */
static bool panel_has_side(const Pattern *p)  { return p->aim == AIM_WALL; }
static bool panel_has_point(const Pattern *p) { return p->origin == ORIGIN_POINT; }
static bool panel_has_gap(const Pattern *p)
{
    return p->aim == AIM_WALL || p->aim == AIM_ENCIRCLE || p->aim == AIM_CROSS;
}
static bool panel_has_radii(const Pattern *p)
{
    return p->aim == AIM_ENCIRCLE || p->aim == AIM_CROSS;
}
static bool panel_has_sprite(const Pattern *p) { return p->sprite[0] != '\0'; }

/* Number of property rows draw_panel() will draw for p. */
static int panel_row_count(const Pattern *p)
{
    int rows = 2;                          /* origin, aim */
    if (panel_has_side(p))  rows += 1;
    if (panel_has_point(p)) rows += 2;
    rows += 9;                             /* count, spread, speed, delay,
                                              repeat, damage, radius, spin,
                                              jitter */
    rows += 2;                             /* angle + has_angle */
    if (panel_has_gap(p))   rows += 1;
    if (panel_has_radii(p)) rows += 2;
    if (p->aim == AIM_FORMATION) rows += 2; /* segments + length */
    else if (p->aim == AIM_SPEAR) rows += 1;
    rows += 1;                             /* sprite picker */
    if (panel_has_sprite(p)) rows += 2;    /* sprite size + rotation */
    rows += 3;                             /* colour R/G/B */
    return rows;
}

static void draw_panel(void)
{
    DrawText("PROPERTIES", PANEL_X, 22, 20, C_DIM);

    Pattern *p = &set.items[sel];
    int row = 0;

    int origin = (int)p->origin;
    if (cycler("origin", row++, &origin, ORIGIN_LABELS, 7)) p->origin = (Origin)origin;
    int aim = (int)p->aim;
    if (cycler("aim", row++, &aim, AIM_LABELS, AIM_LABEL_COUNT)) {
        p->aim = (Aim)aim;
        reset_preview();
    }
    if (panel_has_side(p)) {
        int side = (int)p->side;
        if (cycler("side (walls)", row++, &side, SIDE_LABELS, 4)) p->side = (Side)side;
    }

    if (panel_has_point(p)) {
        if (slider_f("point x", row++, &p->point.x, 0.0f, (float)ARENA_W, 1.0f)) {}
        if (slider_f("point y", row++, &p->point.y, 0.0f, (float)ARENA_H, 1.0f)) {}
    }

    /* For a spear, "count" is the number of bullets in the shaft; for a
       formation it is how many bullets each vertex fires. */
    const char *count_label = (p->aim == AIM_SPEAR) ? "shaft bullets" : "count";
    if (slider_i(count_label, row++, &p->count, 1, 128)) reset_preview();

    float spread = p->spread * (180.0f / PI);
    if (slider_f("spread deg", row++, &spread, 0.0f, 360.0f, 1.0f)) p->spread = spread * (PI / 180.0f);

    if (slider_f("speed", row++, &p->speed, 0.0f, 800.0f, 5.0f)) {}
    if (slider_f("delay s", row++, &p->delay, 0.0f, 2.0f, 0.01f)) {}
    if (slider_i("repeat", row++, &p->repeat, 0, 200)) {}

    if (slider_i("damage", row++, &p->damage, 1, 50)) {}
    if (slider_f("radius", row++, &p->radius, 1.0f, 40.0f, 0.5f)) {}

    float deg_per_shot = p->spin * (180.0f / PI);
    if (slider_f("spin deg/shot", row++, &deg_per_shot, -180.0f, 180.0f, 1.0f)) p->spin = deg_per_shot * (PI / 180.0f);

    float jitter = p->jitter * (180.0f / PI);
    if (slider_f("jitter deg", row++, &jitter, 0.0f, 90.0f, 1.0f)) p->jitter = jitter * (PI / 180.0f);

    float angle = p->angle * (180.0f / PI);
    if (slider_f("angle deg", row++, &angle, -180.0f, 180.0f, 1.0f)) p->angle = angle * (PI / 180.0f);

    if (checkbox("angle is set", row++, &p->has_angle)) {}

    /* gap: -1 random, 0 none, n>0 means "leave slot n-1 empty" */
    if (panel_has_gap(p)) {
        if (gap_widget("gap", row++, &p->gap)) {}
    }

    if (panel_has_radii(p)) {
        if (slider_f("spawn radius", row++, &p->spawn_radius, 0.0f, 600.0f, 5.0f)) {}
        if (slider_f("min radius", row++, &p->min_radius, 0.0f, 400.0f, 5.0f)) {}
    }
    if (p->aim == AIM_FORMATION) {
        if (slider_i("segments", row++, &p->segments, 1, 20)) reset_preview();
        if (slider_f("length", row++, &p->length, 0.0f, 400.0f, 5.0f)) {}
    } else if (p->aim == AIM_SPEAR) {
        if (slider_f("length", row++, &p->length, 0.0f, 400.0f, 5.0f)) reset_preview();
    }

    /* Bullet look: any png dropped into resources/ can be picked here. */
    int sprite = sprite_index(p->sprite);
    if (cycler("sprite", row++, &sprite, sprite_labels, sprite_choice_count)) {
        snprintf(p->sprite, SPRITE_NAME_MAX, "%s", sprite_choices[sprite]);
    }
    if (panel_has_sprite(p)) {
        if (p->sprite_size <= 0.0f) p->sprite_size = p->radius * 2.0f;
        if (slider_f("sprite size", row++, &p->sprite_size, 2.0f, 96.0f, 1.0f)) {}
        static const char *const ROT_LABELS[] = { "upright", "face travel" };
        int rot = (int)p->sprite_rot;
        if (cycler("sprite rot", row++, &rot, ROT_LABELS, 2)) p->sprite_rot = (SpriteRot)rot;
    }

    int r = p->color.r;
    if (slider_i("colour R", row++, &r, 0, 255)) p->color.r = (unsigned char)r;
    int g = p->color.g;
    if (slider_i("colour G", row++, &g, 0, 255)) p->color.g = (unsigned char)g;
    int b = p->color.b;
    if (slider_i("colour B", row++, &b, 0, 255)) p->color.b = (unsigned char)b;

    if ((int)p->color.a == 0) p->color.a = 255;
}

/* --------------------------------------------------------------- update */
static void update_sim(void)
{
    float dt = GetFrameTime();
    if (dt > 0.05f) dt = 0.05f;

    Vector2 m = GetMousePosition();
    if (m.x >= CANVAS_X && m.x < CANVAS_X + CANVAS_W && m.y >= CANVAS_Y && m.y < CANVAS_Y + CANVAS_H) {
        player_x = (m.x - CANVAS_X) / CANVAS_SCALE;
        player_y = (m.y - CANVAS_Y) / CANVAS_SCALE;
    }

    if (paused || count <= 0) return;

    Pattern p = pattern_apply_defaults(set.items[sel]);

    shot_timer -= dt;
    if (shot_timer <= 0.0f) {
        spawn_from_pattern(&bullets, &p, ARENA_W, ARENA_H, player_x, player_y, &spin);
        shot_timer = (p.delay > 0.01f) ? p.delay : 0.1f;
        shots_fired++;
        if (p.repeat > 0 && shots_fired >= p.repeat) {
            shots_fired = 0;
            spin.angle = 0.0f;
        }
    }

    update_bullets(&bullets, dt, ARENA_W, ARENA_H);
}

static void handle_name_input(void)
{
    int c = GetCharPressed();
    size_t len = strlen(name_buf);
    while (c > 0) {
        if (c >= 32 && c < 127 && len < PATTERN_NAME_MAX - 1) {
            name_buf[len++] = (char)c;
            name_buf[len] = '\0';
        }
        c = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && len > 0) name_buf[len - 1] = '\0';

    if (IsKeyPressed(KEY_ENTER)) {
        if (name_buf[0]) snprintf(set.names[sel], PATTERN_NAME_MAX, "%s", name_buf);
        fix_names();
        editing_name = false;
        say("renamed");
    }
    if (IsKeyPressed(KEY_ESCAPE)) editing_name = false;
}

/* ---------------------------------------------------------------- main */
static int export_defaults(const char *target)
{
    PatternSet tmp;
    memset(&tmp, 0, sizeof tmp);

    int n = PATTERN_COUNT;
    if (n > MAX_PATTERNS) n = MAX_PATTERNS;

    for (int i = 0; i < n; i++) {
        tmp.items[i] = PATTERNS[i];
        snprintf(tmp.names[i], PATTERN_NAME_MAX, "%s", (PATTERNS[i].name && *PATTERNS[i].name) ? PATTERNS[i].name : "unnamed");
    }
    for (int i = 0; i < n; i++) tmp.items[i].name = tmp.names[i];

    if (!PatternsSave(target, tmp.items, n)) {
        fprintf(stderr, "error: could not write '%s'\n", target);
        return 1;
    }
    printf("wrote %d patterns to %s\n", n, target);
    return 0;
}

static bool same_float(float a, float b)
{
    float d = fabsf(a - b);
    return d <= 1e-3f || d <= 1e-4f * fabsf(b);
}

static int check_file(const char *target)
{
    PatternSet tmp;
    char err[256] = { 0 };
    int n = PatternsLoad(target, &tmp, err, sizeof err);
    if (n < 0) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }

    int problems = 0;
    if (n != PATTERN_COUNT) {
        printf("pattern count differs: file %d, built-in %d\n", n, PATTERN_COUNT);
        problems++;
    }

    int common = (n < PATTERN_COUNT) ? n : PATTERN_COUNT;
    for (int i = 0; i < common; i++) {
        Pattern a = pattern_apply_defaults(tmp.items[i]);
        Pattern b = pattern_apply_defaults(PATTERNS[i]);
        bool ok = (strcmp(a.name, b.name) == 0) &&
                  (a.origin == b.origin) && (a.aim == b.aim) && (a.side == b.side) &&
                  (a.count == b.count) && (a.repeat == b.repeat) && (a.damage == b.damage) &&
                  (a.gap == b.gap) && (a.has_angle == b.has_angle) &&
                  (a.color.r == b.color.r) && (a.color.g == b.color.g) && (a.color.b == b.color.b) &&
                  same_float(a.point.x, b.point.x) && same_float(a.point.y, b.point.y) &&
                  same_float(a.spread, b.spread) && same_float(a.speed, b.speed) &&
                  same_float(a.delay, b.delay) && same_float(a.radius, b.radius) &&
                  same_float(a.spin, b.spin) && same_float(a.jitter, b.jitter) &&
                  same_float(a.angle, b.angle) &&
                  same_float(a.spawn_radius, b.spawn_radius) && same_float(a.min_radius, b.min_radius) &&
                  (a.segments == b.segments) && same_float(a.length, b.length) &&
                  (strcmp(a.sprite, b.sprite) == 0) &&
                  same_float(a.sprite_size, b.sprite_size) && (a.sprite_rot == b.sprite_rot);
        if (!ok) {
            printf("pattern %d ('%s') differs from the built-in table\n", i, a.name);
            problems++;
        }
    }

    if (problems == 0) printf("OK: %d patterns match the built-in table\n", n);
    else printf("%d difference(s) found\n", problems);
    return problems == 0 ? 0 : 1;
}

/* Every pattern the editor can show must fit its property panel inside the
   window; a row that falls off the bottom is unreachable, so this is a hard
   error rather than a warning. */
static int check_layout(void)
{
    int problems = 0;
    int max_rows = 0;
    const Pattern *worst = &PATTERNS[0];

    for (int i = 0; i < PATTERN_COUNT; i++) {
        int rows = panel_row_count(&PATTERNS[i]);
        if (rows > max_rows) { max_rows = rows; worst = &PATTERNS[i]; }
        int bottom = PANEL_Y + rows * PROP_ROW_H;
        if (bottom > WIN_H) {
            printf("FAIL '%s' draws %d rows, panel bottom y=%d > window height %d\n",
                   PATTERNS[i].name, rows, bottom, WIN_H);
            problems++;
        }
    }

    /* The tallest column of controls the panel can ever produce, which is a
       point-origin formation with a sprite (every optional block shown at once). */
    Pattern tall = pattern_apply_defaults((Pattern){ 0 });
    tall.name = "tallest";
    tall.origin = ORIGIN_POINT;
    tall.aim = AIM_FORMATION;
    snprintf(tall.sprite, SPRITE_NAME_MAX, "x");
    int tall_rows = panel_row_count(&tall);
    int tall_bottom = PANEL_Y + tall_rows * PROP_ROW_H;
    if (tall_bottom > WIN_H) {
        printf("FAIL tallest possible panel draws %d rows, bottom y=%d > %d\n",
               tall_rows, tall_bottom, WIN_H);
        problems++;
    }

    if (problems == 0) {
        printf("OK: panel fits (%d rows worst case over the built-in table, "
               "%d rows tallest possible, window %dpx)\n",
               max_rows, tall_rows, WIN_H);
        printf("    max pattern '%s', bottom y=%d (of %d)\n",
               worst->name, PANEL_Y + max_rows * PROP_ROW_H, WIN_H);
    }
    return problems == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *arg_path = NULL;

    if (argc > 1) {
        if (strcmp(argv[1], "--export") == 0) {
            return export_defaults((argc > 2) ? argv[2] : "resources/patterns.txt");
        }
        if (strcmp(argv[1], "--check") == 0) {
            return check_file((argc > 2) ? argv[2] : "resources/patterns.txt");
        }
        if (strcmp(argv[1], "--layout") == 0) {
            return check_layout();
        }
        if (argv[1][0] != '-') arg_path = argv[1];
    }

    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(WIN_W, WIN_H, "Bullet Hell - Pattern Editor");
    if (!IsWindowReady()) {
        fprintf(stderr, "error: could not create a window\n");
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    load_initial(arg_path);

    canvas = LoadRenderTexture(ARENA_W, ARENA_H);
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_BILINEAR);

    while (!WindowShouldClose()) {
        if (editing_name) {
            handle_name_input();
        } else {
            if (IsKeyPressed(KEY_SPACE)) paused = !paused;
            if (IsKeyPressed(KEY_R)) { reset_preview(); say("preview restarted"); }
            if (IsKeyPressed(KEY_DOWN)) select_pattern(sel + 1);
            if (IsKeyPressed(KEY_UP)) select_pattern(sel - 1);
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_S)) do_save();
            if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_R)) do_reload();
        }

        update_sim();

        BeginDrawing();
        ClearBackground(C_BG);

        DrawText("BULLET HELL  -  pattern editor", LIST_X, 22, 22, C_ACCENT);
        DrawText("patterns.txt", LIST_X, 46, 14, C_DIM);

        draw_canvas();
        draw_list();
        draw_info();
        draw_panel();

        EndDrawing();
    }

    UnloadRenderTexture(canvas);
    CloseWindow();
    return 0;
}
