#ifndef UI_H
#define UI_H

#include "raylib.h"
#include <stdbool.h>

/* Immediate-mode UI used by the editor (and any tool that wants the engine
   look). The theme is a dark dockable-workbench palette: rounded panels,
   hairline borders, an accent selection colour, compact rows and primitive-
   drawn icons. Widgets take an explicit id so hover/drag/focus state can be
   tracked without a retained widget tree. */

#define UI_MAX_CLIP 8

typedef struct {
    Color background;
    Color panel;
    Color panel_alt;
    Color header;
    Color header_active;
    Color border;
    Color border_light;
    Color text;
    Color text_dim;
    Color text_disabled;
    Color accent;
    Color accent_hover;
    Color accent_dim;
    Color selection;
    Color selection_edge;
    Color success;
    Color warning;
    Color error;
    Color viewport;
    Color grid;
    Color grid_major;
    Color shadow;
    Color row;
    Color row_hover;
    Font font;
    int font_small;
    int font_normal;
    int font_large;
    int font_title;
    float radius;
} UITheme;

typedef enum {
    UI_ICON_NONE = 0,
    UI_ICON_PLAY,
    UI_ICON_PAUSE,
    UI_ICON_STOP,
    UI_ICON_STEP,
    UI_ICON_SAVE,
    UI_ICON_RELOAD,
    UI_ICON_MOVE,
    UI_ICON_ROTATE,
    UI_ICON_SCALE,
    UI_ICON_GRID,
    UI_ICON_SNAP,
    UI_ICON_PLUS,
    UI_ICON_TRASH,
    UI_ICON_DUPLICATE,
    UI_ICON_EYE,
    UI_ICON_EYE_OFF,
    UI_ICON_FOLDER,
    UI_ICON_SCRIPT,
    UI_ICON_TEXTURE,
    UI_ICON_ENTITY,
    UI_ICON_CAMERA,
    UI_ICON_CONSOLE,
    UI_ICON_SEARCH,
    UI_ICON_CHECK,
    UI_ICON_CLOSE,
    UI_ICON_CHEVRON_DOWN,
    UI_ICON_CHEVRON_RIGHT,
    UI_ICON_LEVELS,
    UI_ICON_SETTINGS,
    UI_ICON_WARNING,
    UI_ICON_INFO
} UIIcon;

void UIInit(void);
void UIShutdown(void);
UITheme *UIThemeGet(void);

void UIBeginFrame(Vector2 mouse);
void UIEndFrame(void);

/* ---------------------------------------------------------------- text */
void UIDrawText(const char *text, float x, float y, int size, Color color);
void UIDrawTextCentered(const char *text, Rectangle r, int size, Color color);
float UITextWidth(const char *text, int size);
void UIClipText(const char *text, int size, float max_width, char *out, int cap);

/* ------------------------------------------------------------- drawing */
void UIShadow(Rectangle r, float roundness, int layers);
void UIPanelFrame(Rectangle r, const char *title);
Rectangle UIPanelBegin(Rectangle r, const char *title);
void UIPanelEnd(void);
void UISectionHeader(Rectangle r, const char *label);
void UIHLine(float x, float y, float width, Color color);

void UIPushClip(Rectangle r);
void UIPopClip(void);
Rectangle UIClipRect(void);
bool UIMouseIn(Rectangle r);
bool UIMouseInRaw(Rectangle r);
void UIBlockPointer(bool block);

/* -------------------------------------------------------------- widgets */
bool UIFlatButton(const char *id, Rectangle r, const char *label, bool active);
bool UIButton(const char *id, Rectangle r, const char *label, bool enabled);
bool UIButtonAccent(const char *id, Rectangle r, const char *label, bool enabled);
bool UIIconButton(const char *id, Rectangle r, UIIcon icon, bool active,
                  bool enabled, const char *tooltip);
bool UIToggleButton(const char *id, Rectangle r, const char *label, bool *value);
bool UICheckbox(const char *id, Rectangle r, const char *label, bool *value);
bool UISelectable(const char *id, Rectangle r, const char *label, bool selected,
                  UIIcon icon, Color icon_color);
bool UICycler(const char *id, Rectangle r, const char *label, int *value,
              const char *const *names, int count);
bool UITextField(const char *id, Rectangle r, char *buf, int cap, bool *editing);
bool UISliderFloat(const char *id, Rectangle r, const char *label, float *value,
                   float min, float max, const char *fmt);
bool UISliderInt(const char *id, Rectangle r, const char *label, int *value,
                 int min, int max, const char *fmt);
bool UIColorButton(const char *id, Rectangle r, const char *label, Color *color);
bool UITreeRow(const char *id, Rectangle r, int depth, const char *label,
               bool selected, bool has_children, bool *expanded,
               UIIcon icon, Color icon_color);

/* Wheel scrolling plus a draggable scrollbar. Returns the current offset. */
float UIScroll(const char *id, Rectangle view, float content_height, float *scroll);

void UIDrawIcon(UIIcon icon, Rectangle r, Color color);
void UITooltip(const char *text);
int  UIHash(const char *text);

#endif
