#include "pause_menu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* "Difficulty: Normal" is assembled here and handed to raylib, which copies it
   into the draw call - so one rotating buffer is safe and keeps the returned
   pointer alive long enough to be drawn. */
#define TEXT_SLOTS 4
#define TEXT_SLOT_LEN 96

/* ---------------- value bindings ---------------- */
/* A binding is what a `value <name>` row points at: the game hands over a
   range and how to read, write and name the live setting.
   shortcut: 16 bindings, and they are registered in C, so a new one needs a
   rebuild - raise BIND_MAX when a game wants more than that at once. */
#define BIND_MAX 16

typedef struct {
    char name[PAUSE_VALUE_MAX];
    int min, max;
    int (*get)(void);
    void (*set)(int);
    PauseValueText text;
} Binding;

static Binding bindings[BIND_MAX];
static int binding_count = 0;

void PauseMenuBindValue(const char *name, int min, int max, int (*get)(void),
                        void (*set)(int), PauseValueText text)
{
    if (!name || !*name || max < min) return;

    Binding *b = NULL;
    for (int i = 0; i < binding_count; i++)
        if (strcmp(bindings[i].name, name) == 0) b = &bindings[i];

    if (!b) {
        if (binding_count >= BIND_MAX) return;
        b = &bindings[binding_count++];
        memset(b, 0, sizeof *b);
        snprintf(b->name, sizeof b->name, "%s", name);
    }

    b->min = min;
    b->max = max;
    b->get = get;
    b->set = set;
    b->text = text;
}

static const Binding *find_binding(const char *name)
{
    if (!name || !*name) return NULL;
    for (int i = 0; i < binding_count; i++)
        if (strcmp(bindings[i].name, name) == 0) return &bindings[i];
    return NULL;
}

/* step by one, wrapping; a single-value range stays put */
int PauseMenuStep(int current, int min, int max, int step)
{
    if (max <= min) return min;
    int v = current + step;
    if (v > max) v = min;
    if (v < min) v = max;
    return v;
}

int PauseMenuValueGet(const MenuItem *item)
{
    if (!item || !item->value[0]) return 0;
    const Binding *b = find_binding(item->value);
    if (!b) return item->current;          /* unbound: the file's own value */
    if (!b->get) return b->min;            /* no game value: whatever the row holds */
    int v = b->get();
    if (v < b->min) return b->min;
    if (v > b->max) return b->max;
    return v;
}

void PauseMenuValueSet(MenuItem *item, int v)
{
    if (!item || !item->value[0]) return;
    const Binding *b = find_binding(item->value);
    if (!b) {
        item->current = v < item->min ? item->min : (v > item->max ? item->max : v);
        return;
    }
    if (v < b->min) v = b->min;
    if (v > b->max) v = b->max;
    if (b->set) b->set(v);
}

const char *PauseMenuItemText(const MenuItem *item)
{
    static char slots[TEXT_SLOTS][TEXT_SLOT_LEN];
    static int next = 0;
    if (!item) return "";
    if (!item->value[0]) return item->label;

    int v = PauseMenuValueGet(item);
    const Binding *b = find_binding(item->value);
    const char *text = (b && b->text) ? b->text(v) : NULL;
    if (!text && !b) {
        /* unbound value: show it as its range, so the author can see it */
        static char range[32];
        snprintf(range, sizeof range, "%d..%d", item->min, item->max);
        text = range;
    }
    if (text && strcmp(text, item->label) == 0) {
        /* the binding already names the value ("Normal"); just the number then */
        static char num[16];
        snprintf(num, sizeof num, "%d", v);
        text = num;
    }

    char *out = slots[next++ % TEXT_SLOTS];
    if (text) snprintf(out, TEXT_SLOT_LEN, "%s: %s", item->label, text);
    else      snprintf(out, TEXT_SLOT_LEN, "%s: %d", item->label, v);
    return out;
}

/* ---------------- defaults ---------------- */
void PauseMenuDefaults(PauseMenu *m)
{
    memset(m, 0, sizeof *m);
    snprintf(m->title, sizeof m->title, "PAUSED");
    snprintf(m->hint, sizeof m->hint, "Up/Down + Enter  |  ESC resumes");

    snprintf(m->items[0].label, sizeof m->items[0].label, "Restart");
    m->items[0].action = MENU_ACTION_RESTART;

    /* bound to the difficulty the game is playing at */
    snprintf(m->items[1].label, sizeof m->items[1].label, "Difficulty");
    snprintf(m->items[1].value, sizeof m->items[1].value, "difficulty");
    m->items[1].min = 0;
    m->items[1].max = 2;

    snprintf(m->items[2].label, sizeof m->items[2].label, "Quit To Menu");
    m->items[2].action = MENU_ACTION_MENU;
    m->count = 3;
}

/* ---------------- parsing ---------------- */
static MenuAction parse_action(const char *word)
{
    if (strcmp(word, "resume")  == 0) return MENU_ACTION_RESUME;
    if (strcmp(word, "restart") == 0) return MENU_ACTION_RESTART;
    if (strcmp(word, "start")   == 0) return MENU_ACTION_START;
    if (strcmp(word, "menu")    == 0) return MENU_ACTION_MENU;
    if (strcmp(word, "quit")    == 0) return MENU_ACTION_QUIT;
    return MENU_ACTION_NONE;
}

/* Fold a menu item's line of the file onto a MenuItem. The three ways an item
   can be made into something the player can hit are mutually exclusive, and
   each clears the others:
       action <name>            a built-in action
       value <name>             a setting, changed with Left/Right
       start <n>                where a value item starts (default: its min)
   An item with none of them is just a label. */
static bool parse_item(PauseMenu *m, const char *p, char *err, int errSize, int lineNo)
{
    MenuItem *it = &m->items[m->count++];
    memset(it, 0, sizeof *it);

    /* A label is one word, or "several words" in quotes - menu labels need
       spaces ("Quit To Menu") and the option keywords are single words. */
    int keyLen = 0;
    if (*p == '"') {
        const char *close = strchr(p + 1, '"');
        if (!close) {
            if (err) snprintf(err, errSize, "line %d: item label is missing its closing quote",
                              lineNo);
            return false;
        }
        size_t len = (size_t)(close - (p + 1));
        if (len == 0 || len >= PAUSE_LABEL_MAX) {
            if (err) snprintf(err, errSize, "line %d: item label must be 1..%d characters",
                              lineNo, PAUSE_LABEL_MAX - 1);
            return false;
        }
        memcpy(it->label, p + 1, len);
        it->label[len] = '\0';
        keyLen = (int)(close + 1 - p);
    } else {
        char key[PAUSE_LABEL_MAX] = { 0 };
        if (sscanf(p, "%39s%n", key, &keyLen) != 1) {
            if (err) snprintf(err, errSize, "line %d: item needs a label", lineNo);
            return false;
        }
        snprintf(it->label, sizeof it->label, "%s", key);
    }

    const Binding *bound = find_binding(it->label);   /* `item Difficulty` is enough */
    if (bound) {
        it->value[0] = 0;
        snprintf(it->value, sizeof it->value, "%s", bound->name);
        it->min = bound->min;
        it->max = bound->max;
        it->current = PauseMenuValueGet(it);
    }
    it->action = MENU_ACTION_NONE;
    bool hasStart = false;

    const char *rest = p + keyLen;
    while (*rest) {
        char word[PAUSE_LABEL_MAX] = { 0 };
        int wlen = 0;
        if (sscanf(rest, " %39s%n", word, &wlen) != 1) break;
        rest += wlen;
        while (*rest == ' ' || *rest == '\t') rest++;

        if (strcmp(word, "action") == 0 || strcmp(word, "value") == 0 ||
            strcmp(word, "start") == 0) {
            const char *head = word;
            char arg[PAUSE_LABEL_MAX] = { 0 };
            int alen = 0;
            if (sscanf(rest, " %39s%n", arg, &alen) != 1) {
                if (err) snprintf(err, errSize, "line %d: %s needs a name", lineNo, head);
                return false;
            }
            rest += alen;

            if (strcmp(head, "action") == 0) {
                MenuAction a = parse_action(arg);
                if (a == MENU_ACTION_NONE) {
                    if (err) snprintf(err, errSize,
                                      "line %d: unknown action '%s' (resume, restart, start, menu, quit)",
                                      lineNo, arg);
                    return false;
                }
                it->action = a;
                it->value[0] = '\0';
            } else if (strcmp(head, "value") == 0) {
                const Binding *b = find_binding(arg);
                if (!b) {
                    if (err) snprintf(err, errSize,
                                      "line %d: no value called '%s' is registered by the game",
                                      lineNo, arg);
                    return false;
                }
                snprintf(it->value, sizeof it->value, "%s", b->name);
                it->min = b->min;
                it->max = b->max;
                it->action = MENU_ACTION_NONE;
            } else {   /* start <n> */
                int v = atoi(arg);
                if (v < it->min) v = it->min;
                if (v > it->max) v = it->max;
                it->current = v;
                hasStart = true;
            }
        } else {
            if (err) snprintf(err, errSize, "line %d: unknown option '%s'", lineNo, word);
            return false;
        }
    }

    /* Only a row that names its own starting point writes to the game: a plain
       `value difficulty` row shows the difficulty already in play. */
    if (it->value[0] && hasStart) PauseMenuValueSet(it, it->current);
    return true;
}

bool PauseMenuLoad(const char *path, PauseMenu *out, char *err, int errSize)
{
    if (!out) return false;
    PauseMenuDefaults(out);
    if (!path) return true;

    FILE *f = fopen(path, "rb");
    if (!f) return true;   /* no file simply means the built-in menu */

    /* parse into a fresh menu so a bad file halfway through cannot leave the
       live one truncated; the defaults stay put unless the whole file parses */
    PauseMenu m;
    memset(&m, 0, sizeof m);
    snprintf(m.title, sizeof m.title, "%s", out->title);
    snprintf(m.hint, sizeof m.hint, "%s", out->hint);

    bool bad = false;
    char line[1024];
    int lineNo = 0;
    while (!bad && fgets(line, sizeof line, f)) {
        lineNo++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        int n = (int)strlen(p);
        while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ' ||
                         p[n - 1] == '\t')) p[--n] = '\0';
        if (!*p) continue;

        char key[PAUSE_LABEL_MAX] = { 0 };
        int keyLen = 0;
        if (sscanf(p, " %39s%n", key, &keyLen) != 1) continue;
        char *rest = p + keyLen;
        while (*rest == ' ' || *rest == '\t') rest++;

        if (strcmp(key, "title") == 0) {
            if (*rest) snprintf(m.title, sizeof m.title, "%s", rest);
        } else if (strcmp(key, "hint") == 0) {
            snprintf(m.hint, sizeof m.hint, "%s", rest);
        } else if (strcmp(key, "item") == 0) {
            if (m.count >= PAUSE_MAX_ITEMS) {
                if (err) snprintf(err, errSize, "line %d: more than %d items",
                                  lineNo, PAUSE_MAX_ITEMS);
                bad = true;
            } else if (!*rest) {
                if (err) snprintf(err, errSize, "line %d: item needs a label", lineNo);
                bad = true;
            } else {
                bad = !parse_item(&m, rest, err, errSize, lineNo);
            }
        } else {
            if (err) snprintf(err, errSize, "line %d: unknown key '%s' (title, hint, item)",
                              lineNo, key);
            bad = true;
        }
    }
    fclose(f);

    if (bad) return false;      /* out keeps the defaults */
    if (m.count <= 0) {
        if (err) snprintf(err, errSize, "no items - using the built-in pause menu");
        return false;
    }
    *out = m;
    return true;
}
