#include "ui.h"
#include "engine.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static UITheme g_theme;
static Font g_default_font;
static bool g_font_loaded = false;
static bool g_initialized = false;

static Vector2 g_mouse = { 0, 0 };
static Rectangle g_clips[UI_MAX_CLIP];
static int g_clip_count = 0;
static bool g_blocked = false;
static bool g_wheel_used = false;
static int g_frame = 0;
static int g_focus_id = 0;
static int g_focus_frame = -1;
static bool *g_focus_editing = NULL;   /* the field that currently owns focus */
static char g_focus_original[512];
static int g_focus_cap = 0;
static int g_active_id = 0;
static char g_tooltip[256];

#define UI_TEXT_SPACING 0.25f
#define UI_HEADER_H 28.0f
#define UI_ROW_H 22.0f

static Color color_lerp(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        (unsigned char)(a.a + (b.a - a.a) * t)
    };
}

static Color color_light(Color c, float amount)
{
    return color_lerp(c, (Color){ 255, 255, 255, c.a }, amount);
}

static Color color_dark(Color c, float amount)
{
    return color_lerp(c, (Color){ 0, 0, 0, c.a }, amount);
}

static float fclamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static Rectangle intersect_rect(Rectangle a, Rectangle b)
{
    float x1 = a.x > b.x ? a.x : b.x;
    float y1 = a.y > b.y ? a.y : b.y;
    float x2 = (a.x + a.width) < (b.x + b.width) ? (a.x + a.width) : (b.x + b.width);
    float y2 = (a.y + a.height) < (b.y + b.height) ? (a.y + a.height) : (b.y + b.height);
    if (x2 <= x1 || y2 <= y1) return (Rectangle){ -10000, -10000, 1, 1 };
    return (Rectangle){ x1, y1, x2 - x1, y2 - y1 };
}

static Font ui_font(void)
{
    return (g_theme.font.texture.id != 0) ? g_theme.font : GetFontDefault();
}

static bool font_is_custom(void)
{
    return g_font_loaded && g_theme.font.texture.id != 0 &&
           g_theme.font.texture.id != g_default_font.texture.id;
}

static void theme_defaults(void)
{
    memset(&g_theme, 0, sizeof g_theme);
    g_theme.background   = (Color){ 16, 18, 23, 255 };
    g_theme.panel        = (Color){ 24, 27, 33, 255 };
    g_theme.panel_alt    = (Color){ 31, 35, 43, 255 };
    g_theme.header       = (Color){ 29, 33, 41, 255 };
    g_theme.header_active= (Color){ 37, 43, 54, 255 };
    g_theme.border       = (Color){ 45, 51, 63, 255 };
    g_theme.border_light = (Color){ 65, 74, 90, 255 };
    g_theme.text         = (Color){ 220, 226, 235, 255 };
    g_theme.text_dim     = (Color){ 137, 147, 163, 255 };
    g_theme.text_disabled= (Color){ 83, 91, 104, 255 };
    g_theme.accent       = (Color){ 66, 150, 250, 255 };
    g_theme.accent_hover = (Color){ 96, 173, 255, 255 };
    g_theme.accent_dim   = (Color){ 34, 72, 123, 255 };
    g_theme.selection    = (Color){ 28, 53, 87, 255 };
    g_theme.selection_edge=(Color){ 66, 150, 250, 255 };
    g_theme.success      = (Color){ 76, 195, 138, 255 };
    g_theme.warning      = (Color){ 226, 164, 61, 255 };
    g_theme.error        = (Color){ 226, 84, 98, 255 };
    g_theme.viewport     = (Color){ 13, 15, 19, 255 };
    g_theme.grid         = (Color){ 27, 31, 39, 255 };
    g_theme.grid_major   = (Color){ 39, 45, 57, 255 };
    g_theme.shadow       = (Color){ 0, 0, 0, 110 };
    g_theme.row          = (Color){ 28, 32, 40, 255 };
    g_theme.row_hover    = (Color){ 38, 44, 55, 255 };
    g_theme.font         = GetFontDefault();
    g_theme.font_small   = 12;
    g_theme.font_normal  = 14;
    g_theme.font_large   = 17;
    g_theme.font_title   = 22;
    g_theme.radius       = 0.14f;
}

static Font try_load_font(const char *path, int size)
{
    Font f = { 0 };
    if (!path || !FileExists(path)) return f;
    f = LoadFontEx(path, size, NULL, 0);
    if (f.texture.id == 0) return f;
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

void UIInit(void)
{
    if (g_initialized) return;
    g_initialized = true;
    g_default_font = GetFontDefault();
    theme_defaults();

    Font font = { 0 };
    char path[4096];
    if (EngineResolveResource("ui_font.ttf", path, sizeof path))
        font = try_load_font(path, 32);
    if (font.texture.id == 0) font = try_load_font("C:/Windows/Fonts/segoeui.ttf", 32);
    if (font.texture.id == 0) font = try_load_font("C:/Windows/Fonts/arial.ttf", 32);
    if (font.texture.id == 0) font = try_load_font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 32);
    if (font.texture.id == 0) font = try_load_font("/System/Library/Fonts/Supplemental/Arial.ttf", 32);

    if (font.texture.id != 0) {
        g_theme.font = font;
        g_font_loaded = true;
    } else {
        g_theme.font = g_default_font;
        g_font_loaded = false;
    }
    g_clip_count = 0;
    g_blocked = false;
    g_focus_id = 0;
    g_focus_editing = NULL;
    g_active_id = 0;
    g_tooltip[0] = '\0';
}

void UIShutdown(void)
{
    if (!g_initialized) return;
    if (font_is_custom()) UnloadFont(g_theme.font);
    g_theme.font = g_default_font;
    g_font_loaded = false;
    g_initialized = false;
    g_clip_count = 0;
}

UITheme *UIThemeGet(void)
{
    return &g_theme;
}

void UIBeginFrame(Vector2 mouse)
{
    g_mouse = mouse;
    g_frame++;
    g_blocked = false;
    g_wheel_used = false;
    g_tooltip[0] = '\0';
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g_active_id = 0;
}

void UIEndFrame(void)
{
    if (g_tooltip[0]) {
        int size = g_theme.font_small;
        float w = UITextWidth(g_tooltip, size) + 18.0f;
        float h = (float)size + 14.0f;
        float x = g_mouse.x + 16.0f;
        float y = g_mouse.y + 20.0f;
        if (x + w > GetScreenWidth() - 6) x = GetScreenWidth() - w - 6;
        if (y + h > GetScreenHeight() - 6) y = g_mouse.y - h - 10;
        Rectangle box = { x, y, w, h };
        UIShadow(box, 0.25f, 2);
        DrawRectangleRounded(box, 0.25f, 8, (Color){ 18, 21, 27, 245 });
        DrawRectangleRoundedLinesEx(box, 0.25f, 8, 1.0f, g_theme.border_light);
        UIDrawText(g_tooltip, x + 9.0f, y + 7.0f, size, g_theme.text);
        g_tooltip[0] = '\0';
    }
    g_blocked = false;
}

void UITooltip(const char *text)
{
    if (!text || !*text) return;
    snprintf(g_tooltip, sizeof g_tooltip, "%s", text);
}

int UIHash(const char *text)
{
    unsigned int h = 2166136261u;
    if (text) while (*text) { h ^= (unsigned char)*text++; h *= 16777619u; }
    return (int)(h & 0x7fffffff);
}

void UIDrawText(const char *text, float x, float y, int size, Color color)
{
    if (!text || !*text || size <= 0) return;
    DrawTextEx(ui_font(), text, (Vector2){ x, y }, (float)size, UI_TEXT_SPACING, color);
}

float UITextWidth(const char *text, int size)
{
    if (!text || !*text) return 0.0f;
    return MeasureTextEx(ui_font(), text, (float)size, UI_TEXT_SPACING).x;
}

void UIDrawTextCentered(const char *text, Rectangle r, int size, Color color)
{
    if (!text) return;
    float w = UITextWidth(text, size);
    UIDrawText(text, r.x + (r.width - w) * 0.5f, r.y + (r.height - size) * 0.5f - 1.0f, size, color);
}

void UIClipText(const char *text, int size, float max_width, char *out, int cap)
{
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (!text) return;
    if (UITextWidth(text, size) <= max_width) { snprintf(out, (size_t)cap, "%s", text); return; }

    const char *ellipsis = "...";
    float ell = UITextWidth(ellipsis, size);
    char temp[512];
    int n = 0;
    while (text[n] && n < (int)sizeof temp - 1) {
        temp[n] = text[n];
        temp[n + 1] = '\0';
        if (UITextWidth(temp, size) + ell > max_width) break;
        n++;
    }
    if (n >= (int)sizeof temp - 4) n = (int)sizeof temp - 4;
    memcpy(out, temp, (size_t)n);
    out[n] = '\0';
    if (n + 3 < cap) strcat(out, ellipsis);
}

void UIShadow(Rectangle r, float roundness, int layers)
{
    if (layers < 1) layers = 1;
    for (int i = layers; i >= 1; i--) {
        Color c = g_theme.shadow;
        c.a = (unsigned char)((int)g_theme.shadow.a / (i + 1));
        Rectangle s = { r.x + 1.0f, r.y + i * 2.0f, r.width, r.height };
        DrawRectangleRounded(s, roundness, 8, c);
    }
}

static void draw_panel_shell(Rectangle r, const char *title)
{
    float header_h = title ? UI_HEADER_H : 0.0f;
    UIShadow(r, g_theme.radius, 3);
    DrawRectangleRounded(r, g_theme.radius, 10, g_theme.panel);
    if (header_h > 0.0f) {
        DrawRectangleRounded(r, g_theme.radius, 10, g_theme.header);
        DrawRectangle((int)r.x, (int)(r.y + header_h), (int)r.width,
                      (int)(r.height - header_h), g_theme.panel);
        DrawRectangle((int)(r.x + 1), (int)(r.y + header_h - 1),
                      (int)(r.width - 2), 1, g_theme.border);
        DrawRectangle((int)(r.x + 10), (int)(r.y + header_h * 0.5f - 4), 8, 8,
                      g_theme.accent);
        UIDrawText(title, r.x + 25.0f, r.y + (header_h - g_theme.font_small) * 0.5f,
                   g_theme.font_small, g_theme.text_dim);
    }
    DrawRectangleRoundedLinesEx(r, g_theme.radius, 10, 1.0f, g_theme.border);
}

void UIPanelFrame(Rectangle r, const char *title)
{
    draw_panel_shell(r, title);
}

Rectangle UIPanelBegin(Rectangle r, const char *title)
{
    draw_panel_shell(r, title);
    float header_h = title ? UI_HEADER_H : 0.0f;
    Rectangle body = { r.x + 1.0f, r.y + header_h + 1.0f, r.width - 2.0f,
                       r.height - header_h - 2.0f };
    if (body.width < 1.0f) body.width = 1.0f;
    if (body.height < 1.0f) body.height = 1.0f;
    UIPushClip(body);
    return body;
}

void UIPanelEnd(void)
{
    UIPopClip();
}

void UISectionHeader(Rectangle r, const char *label)
{
    UIDrawText(label, r.x + 8.0f, r.y + (r.height - g_theme.font_small) * 0.5f,
               g_theme.font_small, g_theme.text_dim);
    float w = UITextWidth(label, g_theme.font_small);
    float x = r.x + 16.0f + w;
    if (x < r.x + r.width - 4.0f)
        DrawRectangle((int)x, (int)(r.y + r.height * 0.5f),
                      (int)(r.x + r.width - x - 4.0f), 1, g_theme.border);
}

void UIHLine(float x, float y, float width, Color color)
{
    DrawRectangle((int)x, (int)y, (int)width, 1, color);
}

void UIPushClip(Rectangle r)
{
    Rectangle c = r;
    if (g_clip_count > 0) c = intersect_rect(g_clips[g_clip_count - 1], r);
    if (g_clip_count < UI_MAX_CLIP) g_clips[g_clip_count++] = c;
    BeginScissorMode((int)c.x, (int)c.y, (int)c.width, (int)c.height);
}

void UIPopClip(void)
{
    if (g_clip_count <= 0) return;
    EndScissorMode();
    g_clip_count--;
    if (g_clip_count > 0) {
        Rectangle c = g_clips[g_clip_count - 1];
        BeginScissorMode((int)c.x, (int)c.y, (int)c.width, (int)c.height);
    }
}

Rectangle UIClipRect(void)
{
    if (g_clip_count > 0) return g_clips[g_clip_count - 1];
    return (Rectangle){ 0, 0, (float)GetScreenWidth(), (float)GetScreenHeight() };
}

bool UIMouseInRaw(Rectangle r)
{
    return g_mouse.x >= r.x && g_mouse.y >= r.y &&
           g_mouse.x < r.x + r.width && g_mouse.y < r.y + r.height;
}

bool UIMouseIn(Rectangle r)
{
    if (g_blocked) return false;
    if (!UIMouseInRaw(r)) return false;
    if (g_clip_count > 0 && !UIMouseInRaw(g_clips[g_clip_count - 1])) return false;
    return true;
}

void UIBlockPointer(bool block) { g_blocked = block; }

static void row_background(Rectangle r, bool hover, bool selected)
{
    if (selected) {
        DrawRectangleRounded(r, 0.22f, 6, g_theme.selection);
        DrawRectangle((int)r.x, (int)r.y + 3, 2, (int)r.height - 6,
                      g_theme.selection_edge);
    } else if (hover) {
        DrawRectangleRounded(r, 0.22f, 6, g_theme.row_hover);
    }
}

bool UIFlatButton(const char *id, Rectangle r, const char *label, bool active)
{
    (void)id;
    bool hover = UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (active) DrawRectangleRounded(r, 0.22f, 6, g_theme.selection);
    else if (hover) DrawRectangleRounded(r, 0.22f, 6, g_theme.row_hover);
    UIDrawTextCentered(label, r, g_theme.font_normal,
                       active ? g_theme.text : (hover ? g_theme.text : g_theme.text_dim));
    return clicked;
}

bool UIButton(const char *id, Rectangle r, const char *label, bool enabled)
{
    (void)id;
    bool hover = enabled && UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    Color bg = hover ? color_light(g_theme.panel_alt, 0.08f) : g_theme.panel_alt;
    if (!enabled) bg = color_dark(g_theme.panel_alt, 0.18f);
    DrawRectangleRounded(r, 0.18f, 8, bg);
    DrawRectangleRoundedLinesEx(r, 0.18f, 8, 1.0f,
                                enabled ? (hover ? g_theme.border_light : g_theme.border)
                                        : color_dark(g_theme.border, 0.3f));
    UIDrawTextCentered(label, r, g_theme.font_normal,
                       enabled ? (hover ? g_theme.text : g_theme.text_dim)
                               : g_theme.text_disabled);
    return clicked;
}

bool UIButtonAccent(const char *id, Rectangle r, const char *label, bool enabled)
{
    (void)id;
    bool hover = enabled && UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    Color bg = !enabled ? color_dark(g_theme.accent_dim, 0.45f)
               : hover ? g_theme.accent_hover : g_theme.accent;
    DrawRectangleRounded(r, 0.18f, 8, bg);
    UIDrawTextCentered(label, r, g_theme.font_normal,
                       enabled ? (Color){ 12, 18, 26, 255 } : g_theme.text_disabled);
    return clicked;
}

bool UIIconButton(const char *id, Rectangle r, UIIcon icon, bool active,
                  bool enabled, const char *tooltip)
{
    (void)id;
    bool hover = enabled && UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (active) DrawRectangleRounded(r, 0.22f, 6, g_theme.selection);
    else if (hover) DrawRectangleRounded(r, 0.22f, 6, g_theme.row_hover);
    Color c = !enabled ? g_theme.text_disabled : (active ? g_theme.accent : (hover ? g_theme.text : g_theme.text_dim));
    Rectangle ir = { r.x + 5.0f, r.y + 5.0f, r.width - 10.0f, r.height - 10.0f };
    UIDrawIcon(icon, ir, c);
    if (hover && tooltip) UITooltip(tooltip);
    return clicked;
}

bool UIToggleButton(const char *id, Rectangle r, const char *label, bool *value)
{
    if (!value) return false;
    bool hover = UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    Color bg = *value ? g_theme.accent_dim : (hover ? g_theme.row_hover : g_theme.panel_alt);
    DrawRectangleRounded(r, 0.18f, 8, bg);
    DrawRectangleRoundedLinesEx(r, 0.18f, 8, 1.0f,
                                *value ? g_theme.accent : g_theme.border);
    UIDrawTextCentered(label, r, g_theme.font_normal,
                       *value ? g_theme.text : g_theme.text_dim);
    if (clicked) *value = !*value;
    (void)id;
    return clicked;
}

bool UICheckbox(const char *id, Rectangle r, const char *label, bool *value)
{
    if (!value) return false;
    bool hover = UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    Rectangle box = { r.x + 4.0f, r.y + (r.height - 15.0f) * 0.5f, 15.0f, 15.0f };
    DrawRectangleRounded(box, 0.22f, 6, *value ? g_theme.accent : g_theme.panel_alt);
    DrawRectangleRoundedLinesEx(box, 0.22f, 6, 1.0f,
                                *value ? g_theme.accent_hover : g_theme.border_light);
    if (*value) {
        float t = 1.8f;
        DrawLineEx((Vector2){ box.x + 3.5f, box.y + 8.0f },
                   (Vector2){ box.x + 6.5f, box.y + 11.0f }, t, (Color){ 12, 18, 26, 255 });
        DrawLineEx((Vector2){ box.x + 6.5f, box.y + 11.0f },
                   (Vector2){ box.x + 12.0f, box.y + 4.5f }, t, (Color){ 12, 18, 26, 255 });
    }
    if (label) UIDrawText(label, r.x + 26.0f, r.y + (r.height - g_theme.font_normal) * 0.5f,
                          g_theme.font_normal, hover ? g_theme.text : g_theme.text_dim);
    if (clicked) *value = !*value;
    (void)id;
    return clicked;
}

bool UISelectable(const char *id, Rectangle r, const char *label, bool selected,
                  UIIcon icon, Color icon_color)
{
    (void)id;
    bool hover = UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    row_background(r, hover, selected);
    float tx = r.x + 8.0f;
    if (icon != UI_ICON_NONE) {
        Rectangle ir = { r.x + 6.0f, r.y + (r.height - 14.0f) * 0.5f, 14.0f, 14.0f };
        UIDrawIcon(icon, ir, selected ? g_theme.accent : icon_color);
        tx = r.x + 27.0f;
    }
    char clipped[256];
    UIClipText(label ? label : "", g_theme.font_normal, r.x + r.width - tx - 8.0f,
               clipped, sizeof clipped);
    UIDrawText(clipped, tx, r.y + (r.height - g_theme.font_normal) * 0.5f,
               g_theme.font_normal, selected ? g_theme.text : (hover ? g_theme.text : g_theme.text_dim));
    return clicked;
}

bool UICycler(const char *id, Rectangle r, const char *label, int *value,
              const char *const *names, int count)
{
    if (!value || !names || count <= 0) return false;
    if (*value < 0) *value = 0;
    if (*value >= count) *value = count - 1;
    (void)id;

    float label_w = r.width * 0.44f;
    if (label_w > 130.0f) label_w = 130.0f;
    Rectangle ctrl = { r.x + label_w, r.y + 1.0f, r.width - label_w, r.height - 2.0f };
    bool hover = UIMouseIn(ctrl);
    bool pressed = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (label) UIDrawText(label, r.x + 4.0f, r.y + (r.height - g_theme.font_small) * 0.5f,
                          g_theme.font_small, g_theme.text_dim);
    DrawRectangleRounded(ctrl, 0.22f, 6, hover ? g_theme.row_hover : g_theme.panel_alt);
    DrawRectangleRoundedLinesEx(ctrl, 0.22f, 6, 1.0f, hover ? g_theme.border_light : g_theme.border);

    Rectangle left = { ctrl.x + 1.0f, ctrl.y, 20.0f, ctrl.height };
    Rectangle right = { ctrl.x + ctrl.width - 21.0f, ctrl.y, 20.0f, ctrl.height };
    DrawTriangle((Vector2){ left.x + 6, left.y + 5 },
                 (Vector2){ left.x + 6, left.y + left.height - 5 },
                 (Vector2){ left.x + 14, left.y + left.height * 0.5f }, g_theme.text_dim);
    DrawTriangle((Vector2){ right.x + 14, right.y + 5 },
                 (Vector2){ right.x + 14, right.y + right.height - 5 },
                 (Vector2){ right.x + 6, right.y + right.height * 0.5f }, g_theme.text_dim);

    char clipped[128];
    UIClipText(names[*value], g_theme.font_small, ctrl.width - 40.0f, clipped, sizeof clipped);
    UIDrawTextCentered(clipped, ctrl, g_theme.font_small, g_theme.text);

    bool changed = false;
    if (pressed) {
        int step = (g_mouse.x < left.x + left.width) ? -1 : 1;
        *value = (*value + step + count) % count;
        changed = true;
    }
    return changed;
}

static int utf8_append(char *buf, int len, int cap, int cp)
{
    if (cp < 0x80) { if (len + 1 < cap) buf[len++] = (char)cp; }
    else if (cp < 0x800) {
        if (len + 2 < cap) {
            buf[len++] = (char)(0xC0 | (cp >> 6));
            buf[len++] = (char)(0x80 | (cp & 0x3F));
        }
    } else if (cp < 0x10000) {
        if (len + 3 < cap) {
            buf[len++] = (char)(0xE0 | (cp >> 12));
            buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[len++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    buf[len] = '\0';
    return len;
}

bool UITextField(const char *id, Rectangle r, char *buf, int cap, bool *editing)
{
    if (!buf || cap <= 0 || !editing) return false;
    int hid = UIHash(id);
    bool changed = false;
    bool clicked = UIMouseIn(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (g_focus_id == hid && !*editing) { g_focus_id = 0; g_focus_editing = NULL; }

    if (clicked) {
        if (g_focus_id != hid && g_focus_editing && g_focus_editing != editing)
            *g_focus_editing = false;
        if (g_focus_id != hid) {
            snprintf(g_focus_original, sizeof g_focus_original, "%s", buf);
            g_focus_cap = cap;
        }
        g_focus_id = hid;
        g_focus_frame = g_frame;
        g_focus_editing = editing;
        *editing = true;
    }

    if (*editing && g_focus_id != hid) {
        if (g_focus_editing && g_focus_editing != editing) *g_focus_editing = false;
        g_focus_id = hid;
        g_focus_frame = g_frame;
        g_focus_editing = editing;
        snprintf(g_focus_original, sizeof g_focus_original, "%s", buf);
        g_focus_cap = cap;
    }

    bool focused = *editing && g_focus_id == hid;
    if (focused) {
        int len = (int)strlen(buf);
        int cp;
        while ((cp = GetCharPressed()) > 0) {
            if (cp < 32) continue;
            len = utf8_append(buf, len, cap, cp);
            changed = true;
        }
        if (IsKeyPressed(KEY_BACKSPACE) && len > 0) {
            do { len--; } while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
            buf[len] = '\0';
            changed = true;
        }
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip) {
                for (int i = 0; clip[i] && len < cap - 1; i++) {
                    unsigned char ch = (unsigned char)clip[i];
                    if (ch == '\n' || ch == '\r' || ch == '\t') continue;
                    buf[len++] = (char)ch;
                    changed = true;
                }
                buf[len] = '\0';
            }
        }

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            *editing = false;
            g_focus_id = 0;
            g_focus_editing = NULL;
            changed = true;
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            snprintf(buf, (size_t)cap, "%s", g_focus_original);
            *editing = false;
            g_focus_id = 0;
            g_focus_editing = NULL;
            changed = true;
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !UIMouseIn(r) &&
                   g_focus_frame != g_frame) {
            *editing = false;
            g_focus_id = 0;
            g_focus_editing = NULL;
            changed = true;
        }
    }

    DrawRectangleRounded(r, 0.18f, 6, focused ? color_light(g_theme.panel_alt, 0.06f) : g_theme.panel_alt);
    DrawRectangleRoundedLinesEx(r, 0.18f, 6, 1.0f,
                                focused ? g_theme.accent : g_theme.border);

    float inner_w = r.width - 16.0f;
    char visible[512];
    if (focused) {
        const char *start = buf;
        while (*start && UITextWidth(start, g_theme.font_normal) > inner_w) start++;
        snprintf(visible, sizeof visible, "%s", start);
    } else {
        UIClipText(buf, g_theme.font_normal, inner_w, visible, sizeof visible);
    }
    UIDrawText(visible, r.x + 8.0f, r.y + (r.height - g_theme.font_normal) * 0.5f,
               g_theme.font_normal, buf[0] ? g_theme.text : g_theme.text_disabled);

    if (focused && ((int)(GetTime() * 2.0) % 2 == 0)) {
        float x = r.x + 8.0f + UITextWidth(visible, g_theme.font_normal);
        if (x < r.x + r.width - 5.0f)
            DrawRectangle((int)x, (int)(r.y + 5.0f), 1, (int)(r.height - 10.0f), g_theme.accent);
    }
    return changed;
}

static bool clamp_slider(Rectangle r, float *value, float min, float max)
{
    float label_w = r.width * 0.42f;
    if (label_w > 130.0f) label_w = 130.0f;
    if (label_w < 54.0f) label_w = 54.0f;
    float track_x = r.x + label_w;
    float track_w = r.width - label_w - 52.0f;
    if (track_w < 20.0f) track_w = 20.0f;
    float t = (g_mouse.x - track_x) / track_w;
    float v = min + fclamp(t, 0.0f, 1.0f) * (max - min);
    if (v != *value) { *value = v; return true; }
    return false;
}

bool UISliderFloat(const char *id, Rectangle r, const char *label, float *value,
                   float min, float max, const char *fmt)
{
    if (!value) return false;
    int hid = UIHash(id);
    bool hover = UIMouseIn(r);
    bool changed = false;
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_active_id = hid;
    if (g_active_id == hid && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (clamp_slider(r, value, min, max)) changed = true;
    }
    if (*value < min) *value = min;
    if (*value > max) *value = max;

    float label_w = r.width * 0.42f;
    if (label_w > 130.0f) label_w = 130.0f;
    if (label_w < 54.0f) label_w = 54.0f;
    float track_x = r.x + label_w;
    float track_w = r.width - label_w - 52.0f;
    if (track_w < 20.0f) track_w = 20.0f;
    float track_y = r.y + r.height * 0.5f;
    float t = (max > min) ? (*value - min) / (max - min) : 0.0f;
    t = fclamp(t, 0.0f, 1.0f);

    if (hover) DrawRectangleRounded(r, 0.2f, 6, g_theme.row);
    UIDrawText(label, r.x + 4.0f, r.y + (r.height - g_theme.font_small) * 0.5f,
               g_theme.font_small, hover ? g_theme.text : g_theme.text_dim);
    DrawRectangleRounded((Rectangle){ track_x, track_y - 2.5f, track_w, 5.0f }, 0.5f, 6,
                         color_dark(g_theme.panel_alt, 0.1f));
    DrawRectangleRounded((Rectangle){ track_x, track_y - 2.5f, track_w * t, 5.0f }, 0.5f, 6,
                         g_theme.accent);
    DrawCircleV((Vector2){ track_x + track_w * t, track_y }, 5.0f,
                (g_active_id == hid || hover) ? g_theme.accent_hover : g_theme.accent);

    char text[48];
    if (fmt) snprintf(text, sizeof text, fmt, *value);
    else snprintf(text, sizeof text, "%.2f", *value);
    float tw = UITextWidth(text, g_theme.font_small);
    UIDrawText(text, r.x + r.width - tw - 4.0f,
               r.y + (r.height - g_theme.font_small) * 0.5f, g_theme.font_small, g_theme.text);
    return changed;
}

bool UISliderInt(const char *id, Rectangle r, const char *label, int *value,
                 int min, int max, const char *fmt)
{
    if (!value) return false;
    float temp = (float)*value;
    float vmin = (float)min;
    float vmax = (float)max;
    float label_w = r.width * 0.42f;
    if (label_w > 130.0f) label_w = 130.0f;
    if (label_w < 54.0f) label_w = 54.0f;
    int hid = UIHash(id);
    bool hover = UIMouseIn(r);
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_active_id = hid;
    bool changed = false;
    if (g_active_id == hid && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float track_x = r.x + label_w;
        float track_w = r.width - label_w - 52.0f;
        if (track_w < 20.0f) track_w = 20.0f;
        float t = fclamp((g_mouse.x - track_x) / track_w, 0.0f, 1.0f);
        int nv = (int)(vmin + t * (vmax - vmin) + 0.5f);
        if (nv != *value) { *value = nv; changed = true; }
    }
    if (*value < min) *value = min;
    if (*value > max) *value = max;
    temp = (float)*value;

    float track_x = r.x + label_w;
    float track_w = r.width - label_w - 52.0f;
    if (track_w < 20.0f) track_w = 20.0f;
    float track_y = r.y + r.height * 0.5f;
    float t = (vmax > vmin) ? (temp - vmin) / (vmax - vmin) : 0.0f;
    if (hover) DrawRectangleRounded(r, 0.2f, 6, g_theme.row);
    UIDrawText(label, r.x + 4.0f, r.y + (r.height - g_theme.font_small) * 0.5f,
               g_theme.font_small, hover ? g_theme.text : g_theme.text_dim);
    DrawRectangleRounded((Rectangle){ track_x, track_y - 2.5f, track_w, 5.0f }, 0.5f, 6,
                         color_dark(g_theme.panel_alt, 0.1f));
    DrawRectangleRounded((Rectangle){ track_x, track_y - 2.5f, track_w * t, 5.0f }, 0.5f, 6,
                         g_theme.accent);
    DrawCircleV((Vector2){ track_x + track_w * t, track_y }, 5.0f,
                (g_active_id == hid || hover) ? g_theme.accent_hover : g_theme.accent);

    char text[48];
    if (fmt) snprintf(text, sizeof text, fmt, *value);
    else snprintf(text, sizeof text, "%d", *value);
    float tw = UITextWidth(text, g_theme.font_small);
    UIDrawText(text, r.x + r.width - tw - 4.0f,
               r.y + (r.height - g_theme.font_small) * 0.5f, g_theme.font_small, g_theme.text);
    return changed;
}

static const Color g_palette[] = {
    { 255, 255, 255, 255 }, { 255, 216, 120, 255 }, { 255, 165, 0, 255 },
    { 255, 70, 70, 255 }, { 255, 140, 200, 255 }, { 200, 120, 255, 255 },
    { 120, 170, 255, 255 }, { 120, 240, 255, 255 }, { 120, 255, 150, 255 },
    { 160, 160, 160, 255 }
};

bool UIColorButton(const char *id, Rectangle r, const char *label, Color *color)
{
    if (!color) return false;
    int hid = UIHash(id);
    bool hover = UIMouseIn(r);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    int count = (int)(sizeof g_palette / sizeof g_palette[0]);
    int current = 0;
    for (int i = 0; i < count; i++)
        if (g_palette[i].r == color->r && g_palette[i].g == color->g && g_palette[i].b == color->b)
            current = i;

    if (hover) DrawRectangleRounded(r, 0.2f, 6, g_theme.row);
    UIDrawText(label, r.x + 4.0f, r.y + (r.height - g_theme.font_small) * 0.5f,
               g_theme.font_small, hover ? g_theme.text : g_theme.text_dim);
    Rectangle sw = { r.x + r.width - 48.0f, r.y + 3.0f, 44.0f, r.height - 6.0f };
    DrawRectangleRounded(sw, 0.25f, 6, *color);
    DrawRectangleRoundedLinesEx(sw, 0.25f, 6, 1.0f, g_theme.border_light);

    bool changed = false;
    if (clicked) {
        current = (current + 1) % count;
        *color = g_palette[current];
        changed = true;
    }
    (void)hid;
    return changed;
}

bool UITreeRow(const char *id, Rectangle r, int depth, const char *label,
               bool selected, bool has_children, bool *expanded,
               UIIcon icon, Color icon_color)
{
    (void)id;
    bool hover = UIMouseIn(r);
    bool pressed = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    row_background(r, hover, selected);

    float x = r.x + 4.0f + depth * 13.0f;
    if (has_children) {
        Rectangle chev = { x, r.y + (r.height - 14.0f) * 0.5f, 14.0f, 14.0f };
        UIDrawIcon((expanded && *expanded) ? UI_ICON_CHEVRON_DOWN : UI_ICON_CHEVRON_RIGHT,
                   chev, g_theme.text_dim);
        x += 16.0f;
    } else {
        x += 6.0f;
    }

    if (icon != UI_ICON_NONE) {
        Rectangle ir = { x, r.y + (r.height - 14.0f) * 0.5f, 14.0f, 14.0f };
        UIDrawIcon(icon, ir, selected ? g_theme.accent : icon_color);
        x += 19.0f;
    }

    char clipped[256];
    UIClipText(label ? label : "", g_theme.font_normal, r.x + r.width - x - 8.0f,
               clipped, sizeof clipped);
    UIDrawText(clipped, x, r.y + (r.height - g_theme.font_normal) * 0.5f,
               g_theme.font_normal, selected ? g_theme.text : (hover ? g_theme.text : g_theme.text_dim));

    if (!pressed) return false;
    if (has_children && g_mouse.x < x - 4.0f) {
        if (expanded) *expanded = !*expanded;
        return false;
    }
    return true;
}

float UIScroll(const char *id, Rectangle view, float content_height, float *scroll)
{
    if (!scroll) return 0.0f;
    float max_scroll = content_height - view.height;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    if (*scroll < 0.0f) *scroll = 0.0f;
    if (*scroll > max_scroll) *scroll = max_scroll;

    bool hover = UIMouseIn(view);
    if (hover && !g_wheel_used && max_scroll > 0.0f) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            *scroll -= wheel * 52.0f;
            g_wheel_used = true;
        }
    }
    if (*scroll < 0.0f) *scroll = 0.0f;
    if (*scroll > max_scroll) *scroll = max_scroll;

    if (max_scroll <= 0.0f) return *scroll;

    Rectangle track = { view.x + view.width - 6.0f, view.y + 2.0f, 4.0f, view.height - 4.0f };
    float thumb_h = track.height * (view.height / content_height);
    if (thumb_h < 24.0f) thumb_h = 24.0f;
    if (thumb_h > track.height) thumb_h = track.height;
    float t = *scroll / max_scroll;
    Rectangle thumb = { track.x, track.y + t * (track.height - thumb_h), track.width, thumb_h };

    int hid = UIHash(id);
    bool track_hover = UIMouseIn(track);
    if (track_hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_active_id = hid;
    if (g_active_id == hid && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float span = track.height - thumb_h;
        if (span > 0.0f) {
            *scroll = (g_mouse.y - track.y - thumb_h * 0.5f) / span * max_scroll;
            if (*scroll < 0.0f) *scroll = 0.0f;
            if (*scroll > max_scroll) *scroll = max_scroll;
            t = *scroll / max_scroll;
            thumb.y = track.y + t * span;
        }
    }
    DrawRectangleRounded(track, 0.5f, 6, g_theme.row);
    DrawRectangleRounded(thumb, 0.5f, 6, (track_hover || g_active_id == hid) ? g_theme.border_light : g_theme.border);
    return *scroll;
}

static Rectangle icon_box(Rectangle r)
{
    float s = r.width < r.height ? r.width : r.height;
    return (Rectangle){ r.x + (r.width - s) * 0.5f, r.y + (r.height - s) * 0.5f, s, s };
}

static Vector2 icon_pt(Rectangle b, float x, float y)
{
    return (Vector2){ b.x + b.width * x, b.y + b.height * y };
}

static void icon_line(Rectangle b, float x1, float y1, float x2, float y2, Color c)
{
    float t = b.width * 0.085f;
    if (t < 1.2f) t = 1.2f;
    DrawLineEx(icon_pt(b, x1, y1), icon_pt(b, x2, y2), t, c);
}

static void icon_tri(Rectangle b, float x1, float y1, float x2, float y2,
                     float x3, float y3, Color c)
{
    DrawTriangle(icon_pt(b, x1, y1), icon_pt(b, x2, y2), icon_pt(b, x3, y3), c);
}

static void icon_rect(Rectangle b, float x, float y, float w, float h, Color c)
{
    DrawRectangleRec((Rectangle){ b.x + b.width * x, b.y + b.height * y,
                                  b.width * w, b.height * h }, c);
}

static void icon_arrow(Rectangle b, float tx, float ty, float angle, float size, Color c)
{
    Vector2 tip = icon_pt(b, tx, ty);
    Vector2 dir = { cosf(angle), sinf(angle) };
    Vector2 base = { tip.x - dir.x * size, tip.y - dir.y * size };
    Vector2 perp = { -dir.y * size * 0.55f, dir.x * size * 0.55f };
    DrawTriangle(tip,
                 (Vector2){ base.x + perp.x, base.y + perp.y },
                 (Vector2){ base.x - perp.x, base.y - perp.y }, c);
}


void UIDrawIcon(UIIcon icon, Rectangle r, Color c)
{
    if (icon == UI_ICON_NONE || r.width < 2.0f || r.height < 2.0f) return;
    Rectangle b = icon_box(r);
    float s = b.width;

    switch (icon) {
    case UI_ICON_PLAY:
        icon_tri(b, 0.32f, 0.18f, 0.32f, 0.82f, 0.80f, 0.50f, c);
        break;
    case UI_ICON_PAUSE:
        icon_rect(b, 0.30f, 0.20f, 0.13f, 0.60f, c);
        icon_rect(b, 0.57f, 0.20f, 0.13f, 0.60f, c);
        break;
    case UI_ICON_STOP:
        icon_rect(b, 0.28f, 0.28f, 0.44f, 0.44f, c);
        break;
    case UI_ICON_STEP:
        icon_tri(b, 0.24f, 0.22f, 0.24f, 0.78f, 0.62f, 0.50f, c);
        icon_rect(b, 0.68f, 0.22f, 0.10f, 0.56f, c);
        break;
    case UI_ICON_SAVE:
        icon_line(b, 0.20f, 0.18f, 0.80f, 0.18f, c);
        icon_line(b, 0.80f, 0.18f, 0.80f, 0.82f, c);
        icon_line(b, 0.80f, 0.82f, 0.20f, 0.82f, c);
        icon_line(b, 0.20f, 0.82f, 0.20f, 0.18f, c);
        icon_line(b, 0.34f, 0.18f, 0.34f, 0.42f, c);
        icon_line(b, 0.34f, 0.42f, 0.66f, 0.42f, c);
        icon_line(b, 0.66f, 0.42f, 0.66f, 0.18f, c);
        icon_line(b, 0.33f, 0.60f, 0.67f, 0.60f, c);
        icon_line(b, 0.33f, 0.72f, 0.67f, 0.72f, c);
        break;
    case UI_ICON_RELOAD: {
        Vector2 center = icon_pt(b, 0.5f, 0.5f);
        DrawRing(center, s * 0.24f, s * 0.32f, -40.0f, 250.0f, 24, c);
        float a = 250.0f * DEG2RAD;
        icon_arrow(b, 0.5f + cosf(a) * 0.28f, 0.5f + sinf(a) * 0.28f, a + PI * 0.5f, s * 0.18f, c);
        break;
    }
    case UI_ICON_MOVE:
        icon_line(b, 0.5f, 0.20f, 0.5f, 0.80f, c);
        icon_line(b, 0.20f, 0.50f, 0.80f, 0.50f, c);
        icon_arrow(b, 0.50f, 0.15f, -PI * 0.5f, s * 0.14f, c);
        icon_arrow(b, 0.50f, 0.85f, PI * 0.5f, s * 0.14f, c);
        icon_arrow(b, 0.15f, 0.50f, PI, s * 0.14f, c);
        icon_arrow(b, 0.85f, 0.50f, 0.0f, s * 0.14f, c);
        break;
    case UI_ICON_ROTATE: {
        Vector2 center = icon_pt(b, 0.5f, 0.52f);
        DrawRing(center, s * 0.25f, s * 0.33f, 30.0f, 310.0f, 24, c);
        float a = 310.0f * DEG2RAD;
        icon_arrow(b, 0.5f + cosf(a) * 0.29f, 0.52f + sinf(a) * 0.29f, a + PI * 0.5f, s * 0.17f, c);
        DrawCircleV(center, s * 0.07f, c);
        break;
    }
    case UI_ICON_SCALE:
        icon_line(b, 0.24f, 0.76f, 0.70f, 0.30f, c);
        icon_line(b, 0.24f, 0.76f, 0.24f, 0.50f, c);
        icon_line(b, 0.24f, 0.76f, 0.50f, 0.76f, c);
        icon_line(b, 0.50f, 0.24f, 0.78f, 0.24f, c);
        icon_line(b, 0.78f, 0.24f, 0.78f, 0.52f, c);
        icon_arrow(b, 0.76f, 0.24f, -PI * 0.5f, s * 0.13f, c);
        break;
    case UI_ICON_GRID:
        icon_line(b, 0.22f, 0.22f, 0.78f, 0.22f, c);
        icon_line(b, 0.22f, 0.50f, 0.78f, 0.50f, c);
        icon_line(b, 0.22f, 0.78f, 0.78f, 0.78f, c);
        icon_line(b, 0.22f, 0.22f, 0.22f, 0.78f, c);
        icon_line(b, 0.50f, 0.22f, 0.50f, 0.78f, c);
        icon_line(b, 0.78f, 0.22f, 0.78f, 0.78f, c);
        break;
    case UI_ICON_SNAP:
        icon_line(b, 0.28f, 0.18f, 0.28f, 0.52f, c);
        icon_line(b, 0.72f, 0.18f, 0.72f, 0.52f, c);
        icon_line(b, 0.28f, 0.52f, 0.28f, 0.64f, c);
        icon_line(b, 0.72f, 0.52f, 0.72f, 0.64f, c);
        icon_line(b, 0.28f, 0.64f, 0.34f, 0.78f, c);
        icon_line(b, 0.72f, 0.64f, 0.66f, 0.78f, c);
        icon_line(b, 0.34f, 0.78f, 0.44f, 0.84f, c);
        icon_line(b, 0.66f, 0.78f, 0.56f, 0.84f, c);
        break;
    case UI_ICON_PLUS:
        icon_line(b, 0.5f, 0.22f, 0.5f, 0.78f, c);
        icon_line(b, 0.22f, 0.5f, 0.78f, 0.5f, c);
        break;
    case UI_ICON_TRASH:
        icon_line(b, 0.22f, 0.26f, 0.78f, 0.26f, c);
        icon_line(b, 0.42f, 0.18f, 0.58f, 0.18f, c);
        icon_line(b, 0.28f, 0.30f, 0.32f, 0.82f, c);
        icon_line(b, 0.72f, 0.30f, 0.68f, 0.82f, c);
        icon_line(b, 0.32f, 0.82f, 0.68f, 0.82f, c);
        icon_line(b, 0.42f, 0.40f, 0.44f, 0.72f, c);
        icon_line(b, 0.58f, 0.40f, 0.56f, 0.72f, c);
        break;
    case UI_ICON_DUPLICATE:
        icon_line(b, 0.22f, 0.32f, 0.62f, 0.32f, c);
        icon_line(b, 0.62f, 0.32f, 0.62f, 0.78f, c);
        icon_line(b, 0.62f, 0.78f, 0.22f, 0.78f, c);
        icon_line(b, 0.22f, 0.78f, 0.22f, 0.32f, c);
        icon_line(b, 0.38f, 0.22f, 0.78f, 0.22f, c);
        icon_line(b, 0.78f, 0.22f, 0.78f, 0.68f, c);
        icon_line(b, 0.78f, 0.68f, 0.62f, 0.68f, c);
        break;
    case UI_ICON_EYE:
    case UI_ICON_EYE_OFF: {
        DrawEllipseLines((int)(b.x + b.width * 0.5f), (int)(b.y + b.height * 0.5f),
                         b.width * 0.34f, b.height * 0.20f, c);
        DrawCircleV(icon_pt(b, 0.5f, 0.5f), s * 0.11f, c);
        if (icon == UI_ICON_EYE_OFF) icon_line(b, 0.20f, 0.20f, 0.80f, 0.80f, c);
        break;
    }
    case UI_ICON_FOLDER:
        icon_line(b, 0.18f, 0.72f, 0.18f, 0.30f, c);
        icon_line(b, 0.18f, 0.30f, 0.42f, 0.30f, c);
        icon_line(b, 0.42f, 0.30f, 0.50f, 0.40f, c);
        icon_line(b, 0.50f, 0.40f, 0.82f, 0.40f, c);
        icon_line(b, 0.82f, 0.40f, 0.82f, 0.72f, c);
        icon_line(b, 0.82f, 0.72f, 0.18f, 0.72f, c);
        break;
    case UI_ICON_SCRIPT:
        icon_line(b, 0.26f, 0.16f, 0.62f, 0.16f, c);
        icon_line(b, 0.62f, 0.16f, 0.76f, 0.32f, c);
        icon_line(b, 0.76f, 0.32f, 0.76f, 0.84f, c);
        icon_line(b, 0.76f, 0.84f, 0.26f, 0.84f, c);
        icon_line(b, 0.26f, 0.84f, 0.26f, 0.16f, c);
        icon_line(b, 0.62f, 0.16f, 0.62f, 0.32f, c);
        icon_line(b, 0.62f, 0.32f, 0.76f, 0.32f, c);
        icon_line(b, 0.36f, 0.50f, 0.66f, 0.50f, c);
        icon_line(b, 0.36f, 0.62f, 0.66f, 0.62f, c);
        icon_line(b, 0.36f, 0.74f, 0.56f, 0.74f, c);
        break;
    case UI_ICON_TEXTURE:
        icon_line(b, 0.18f, 0.20f, 0.82f, 0.20f, c);
        icon_line(b, 0.82f, 0.20f, 0.82f, 0.80f, c);
        icon_line(b, 0.82f, 0.80f, 0.18f, 0.80f, c);
        icon_line(b, 0.18f, 0.80f, 0.18f, 0.20f, c);
        icon_tri(b, 0.26f, 0.70f, 0.45f, 0.42f, 0.60f, 0.70f, c);
        icon_tri(b, 0.48f, 0.70f, 0.64f, 0.50f, 0.76f, 0.70f, c);
        DrawCircleV(icon_pt(b, 0.66f, 0.34f), s * 0.05f, c);
        break;
    case UI_ICON_ENTITY:
        icon_line(b, 0.5f, 0.16f, 0.82f, 0.46f, c);
        icon_line(b, 0.82f, 0.46f, 0.5f, 0.84f, c);
        icon_line(b, 0.5f, 0.84f, 0.18f, 0.46f, c);
        icon_line(b, 0.18f, 0.46f, 0.5f, 0.16f, c);
        DrawCircleV(icon_pt(b, 0.5f, 0.5f), s * 0.08f, c);
        break;
    case UI_ICON_CAMERA:
        icon_line(b, 0.20f, 0.34f, 0.62f, 0.34f, c);
        icon_line(b, 0.62f, 0.34f, 0.62f, 0.70f, c);
        icon_line(b, 0.62f, 0.70f, 0.20f, 0.70f, c);
        icon_line(b, 0.20f, 0.70f, 0.20f, 0.34f, c);
        icon_tri(b, 0.66f, 0.42f, 0.82f, 0.34f, 0.82f, 0.58f, c);
        DrawCircleLines((int)(b.x + b.width * 0.41f), (int)(b.y + b.height * 0.52f), s * 0.12f, c);
        break;
    case UI_ICON_CONSOLE:
        icon_line(b, 0.18f, 0.22f, 0.82f, 0.22f, c);
        icon_line(b, 0.82f, 0.22f, 0.82f, 0.78f, c);
        icon_line(b, 0.82f, 0.78f, 0.18f, 0.78f, c);
        icon_line(b, 0.18f, 0.78f, 0.18f, 0.22f, c);
        icon_line(b, 0.28f, 0.42f, 0.40f, 0.50f, c);
        icon_line(b, 0.40f, 0.50f, 0.28f, 0.58f, c);
        icon_line(b, 0.48f, 0.60f, 0.66f, 0.60f, c);
        break;
    case UI_ICON_SEARCH:
        DrawCircleLines((int)(b.x + b.width * 0.44f), (int)(b.y + b.height * 0.42f), s * 0.22f, c);
        icon_line(b, 0.60f, 0.60f, 0.78f, 0.80f, c);
        break;
    case UI_ICON_CHECK:
        icon_line(b, 0.22f, 0.52f, 0.42f, 0.72f, c);
        icon_line(b, 0.42f, 0.72f, 0.80f, 0.28f, c);
        break;
    case UI_ICON_CLOSE:
        icon_line(b, 0.26f, 0.26f, 0.74f, 0.74f, c);
        icon_line(b, 0.74f, 0.26f, 0.26f, 0.74f, c);
        break;
    case UI_ICON_CHEVRON_DOWN:
        icon_line(b, 0.28f, 0.40f, 0.50f, 0.62f, c);
        icon_line(b, 0.50f, 0.62f, 0.72f, 0.40f, c);
        break;
    case UI_ICON_CHEVRON_RIGHT:
        icon_line(b, 0.40f, 0.28f, 0.62f, 0.50f, c);
        icon_line(b, 0.62f, 0.50f, 0.40f, 0.72f, c);
        break;
    case UI_ICON_LEVELS:
        icon_rect(b, 0.20f, 0.22f, 0.60f, 0.12f, c);
        icon_rect(b, 0.26f, 0.44f, 0.54f, 0.12f, c);
        icon_rect(b, 0.32f, 0.66f, 0.48f, 0.12f, c);
        break;
    case UI_ICON_SETTINGS: {
        Vector2 center = icon_pt(b, 0.5f, 0.5f);
        DrawRing(center, s * 0.18f, s * 0.30f, 0.0f, 360.0f, 20, c);
        for (int i = 0; i < 8; i++) {
            float a = (float)i * PI / 4.0f;
            float r1 = s * 0.30f, r2 = s * 0.38f;
            icon_line(b,
                     0.5f + cosf(a) * r1 / s, 0.5f + sinf(a) * r1 / s,
                     0.5f + cosf(a) * r2 / s, 0.5f + sinf(a) * r2 / s, c);
        }
        DrawCircleV(center, s * 0.08f, c);
        break;
    }
    case UI_ICON_WARNING:
        icon_line(b, 0.50f, 0.16f, 0.86f, 0.82f, c);
        icon_line(b, 0.86f, 0.82f, 0.14f, 0.82f, c);
        icon_line(b, 0.14f, 0.82f, 0.50f, 0.16f, c);
        icon_line(b, 0.50f, 0.40f, 0.50f, 0.62f, c);
        DrawCircleV(icon_pt(b, 0.50f, 0.72f), s * 0.035f, c);
        break;
    case UI_ICON_INFO:
        DrawCircleLines((int)(b.x + b.width * 0.5f), (int)(b.y + b.height * 0.5f), s * 0.34f, c);
        DrawCircleV(icon_pt(b, 0.50f, 0.30f), s * 0.045f, c);
        icon_line(b, 0.50f, 0.44f, 0.50f, 0.70f, c);
        break;
    default:
        break;
    }
}
