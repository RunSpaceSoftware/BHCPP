#include "input.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_ACTIONS  32
#define MAX_BINDINGS 96
#define ACTION_NAME  32

typedef struct {
    char action[ACTION_NAME];
    int key;
} Binding;

static Binding bindings[MAX_BINDINGS];
static int binding_count = 0;

void InputReset(void) { binding_count = 0; }

bool InputBind(const char *action, int key)
{
    if (!action || !*action || key < 0) return false;
    if (binding_count >= MAX_BINDINGS) return false;
    Binding *b = &bindings[binding_count++];
    snprintf(b->action, sizeof b->action, "%s", action);
    b->key = key;
    return true;
}

static bool any_down(const char *action, int state)
{
    if (!action) return false;
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].action, action) != 0) continue;
        int key = bindings[i].key;
        if (state == 0 && IsKeyDown(key)) return true;
        if (state == 1 && IsKeyPressed(key)) return true;
        if (state == 2 && IsKeyReleased(key)) return true;
    }
    return false;
}

bool InputActionDown(const char *action) { return any_down(action, 0); }
bool InputActionPressed(const char *action) { return any_down(action, 1); }
bool InputActionReleased(const char *action) { return any_down(action, 2); }

float InputAxis(const char *negative, const char *positive)
{
    float v = 0.0f;
    if (InputActionDown(negative)) v -= 1.0f;
    if (InputActionDown(positive)) v += 1.0f;
    return v;
}

Vector2 InputMoveVector(const char *left, const char *right,
                        const char *up, const char *down)
{
    Vector2 v = { InputAxis(left, right), InputAxis(up, down) };
    float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len > 1.0f) { v.x /= len; v.y /= len; }
    return v;
}
