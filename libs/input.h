#ifndef INPUT_H
#define INPUT_H

#include "raylib.h"
#include <stdbool.h>

/* A tiny action map over raylib's key state. Clients bind a name to one or
   more keys once (InputBind can be called repeatedly to add alternatives),
   then ask about the name instead of scattering raw KEY_* checks through the
   game. */

void InputReset(void);
bool InputBind(const char *action, int key);
bool InputActionDown(const char *action);
bool InputActionPressed(const char *action);
bool InputActionReleased(const char *action);

/* -1, 0 or 1 from two actions (negative first). */
float InputAxis(const char *negative, const char *positive);
/* Normalised (-1..1) pair from four actions. */
Vector2 InputMoveVector(const char *left, const char *right,
                        const char *up, const char *down);

#endif
