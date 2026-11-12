#ifndef PAUSE_MENU_H
#define PAUSE_MENU_H

#include <stdbool.h>

/* The pause menu, described by resources/pause.txt instead of by C.
   A dev writes the items they want, in the order they want them:

       title Paused
       hint Up/Down + Enter  |  ESC resumes
       item Resume action resume
       item Difficulty value difficulty
       item Restart action restart
       item Quit To Menu action menu

   The label is free text; the options follow it on the same line. `resume`,
   `restart`, `start`, `menu` and `quit` are the built-in actions, and
   `value <name>` makes the item a setting the player changes with
   Left/Right, where <name> is a binding registered with PauseMenuBindValue().
   No file, no bindings or a bad file all fall back to the built-in menu, so
   the game never ends up unpausable.

   PauseMenuLoad() takes a path and stays clear of raylib so the headless
   checks can exercise it; main.c resolves the resource path.
*/

#define PAUSE_LABEL_MAX 40
#define PAUSE_VALUE_MAX 40
#define PAUSE_TITLE_MAX 64
#define PAUSE_HINT_MAX  160
#define PAUSE_MAX_ITEMS 12

typedef enum {
    MENU_ACTION_NONE = 0,   /* value items only: Left/Right change them */
    MENU_ACTION_RESUME,
    MENU_ACTION_RESTART,
    MENU_ACTION_START,
    MENU_ACTION_MENU,
    MENU_ACTION_QUIT,
} MenuAction;

typedef struct {
    char label[PAUSE_LABEL_MAX];
    MenuAction action;
    char value[PAUSE_VALUE_MAX];   /* binding name, empty when not a value item */
    int min, max, current;
} MenuItem;

typedef struct {
    char title[PAUSE_TITLE_MAX];
    char hint[PAUSE_HINT_MAX];
    MenuItem items[PAUSE_MAX_ITEMS];
    int count;
} PauseMenu;

/* Current value of a value item, as text for the menu ("Normal", "3"); read
   back through PauseMenuValueSet() when the player steps it. */
typedef const char *(*PauseValueText)(int v);

/* Register a value the menu may expose. Re-registering a name replaces it.
   `get` may be NULL, in which case the row keeps the number it was given. */
void PauseMenuBindValue(const char *name, int min, int max, int (*get)(void),
                        void (*set)(int), PauseValueText text);

/* The built-in menu, used when the file is missing or bad. */
void PauseMenuDefaults(PauseMenu *m);

/* Load; a missing file or a bad one keeps the defaults and returns false with
   a message in err (pointing at the line at fault). */
bool PauseMenuLoad(const char *path, PauseMenu *out, char *err, int errSize);

int  PauseMenuValueGet(const MenuItem *item);
void PauseMenuValueSet(MenuItem *item, int v);
/* The label with its value: "Difficulty: Normal". Plain items come back as-is. */
const char *PauseMenuItemText(const MenuItem *item);

/* Clamp a freshly loaded item's current value into its range (and wrap a
   stepped one); exposed so the checks can drive it. */
int PauseMenuStep(int current, int min, int max, int step);

#endif
