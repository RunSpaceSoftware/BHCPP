#include "scene.h"
#include "engine.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return false;
    *out = (int)v;
    return true;
}

static bool parse_float(const char *s, float *out)
{
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end) return false;
    *out = v;
    return true;
}

static bool parse_quoted(const char *s, char *out, int cap)
{
    if (!s || !out || cap <= 0) return false;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s != '"') {
        snprintf(out, (size_t)cap, "%s", s);
        return true;
    }
    s++;
    int n = 0;
    while (*s && *s != '"' && n < cap - 1) {
        if (*s == '\\' && s[1]) s++;
        out[n++] = *s++;
    }
    out[n] = '\0';
    return *s == '"' || *s == '\0';
}

static void entity_defaults(Entity *e)
{
    memset(e, 0, sizeof *e);
    e->parent = -1;
    e->scale = (Vector2){ 1.0f, 1.0f };
    e->visible = true;
    e->sprite_size = (Vector2){ 32.0f, 32.0f };
    e->tint = WHITE;
    e->collider_radius = 12.0f;
}

void SceneInit(Scene *s, const char *name)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name ? name : "Scene");
}

void SceneClear(Scene *s)
{
    if (!s) return;
    char name[SCENE_NAME_MAX];
    snprintf(name, sizeof name, "%s", s->name);
    SceneInit(s, name);
}

int SceneAdd(Scene *s, const char *name)
{
    return SceneInsert(s, s ? s->count : 0, name);
}

int SceneInsert(Scene *s, int index, const char *name)
{
    if (!s || s->count >= SCENE_MAX_ENTITIES) return -1;
    if (index < 0) index = 0;
    if (index > s->count) index = s->count;

    for (int i = s->count; i > index; i--) {
        s->items[i] = s->items[i - 1];
        if (s->items[i].parent >= index) s->items[i].parent++;
    }
    s->count++;

    Entity *e = &s->items[index];
    entity_defaults(e);
    snprintf(e->name, sizeof e->name, "%s", name && *name ? name : "Entity");
    return index;
}

void SceneRemove(Scene *s, int index)
{
    if (!s || index < 0 || index >= s->count) return;
    int parent = s->items[index].parent;

    for (int i = 0; i < s->count; i++) {
        if (i == index) continue;
        if (s->items[i].parent == index) s->items[i].parent = parent;
        else if (s->items[i].parent > index) s->items[i].parent--;
    }
    for (int i = index; i < s->count - 1; i++) s->items[i] = s->items[i + 1];
    s->count--;
}

int SceneDuplicate(Scene *s, int index)
{
    if (!s || index < 0 || index >= s->count || s->count >= SCENE_MAX_ENTITIES) return -1;
    Entity copy = s->items[index];
    /* Inserting before the parent shifts every later index, including the
       copy's own parent link. */
    if (copy.parent > index) copy.parent++;
    int at = SceneInsert(s, index + 1, copy.name);
    if (at < 0) return -1;
    s->items[at] = copy;
    snprintf(s->items[at].name, sizeof s->items[at].name, "%.52s Copy", copy.name);
    return at;
}

int SceneFind(const Scene *s, const char *name)
{
    if (!s || !name) return -1;
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->items[i].name, name) == 0) return i;
    return -1;
}

int SceneFindTag(const Scene *s, const char *tag)
{
    if (!s || !tag) return -1;
    for (int i = 0; i < s->count; i++)
        if (strcmp(s->items[i].tag, tag) == 0) return i;
    return -1;
}

Entity *SceneEntity(Scene *s, int index)
{
    if (!s || index < 0 || index >= s->count) return NULL;
    return &s->items[index];
}

const Entity *SceneEntityConst(const Scene *s, int index)
{
    if (!s || index < 0 || index >= s->count) return NULL;
    return &s->items[index];
}

bool SceneSetParent(Scene *s, int child, int parent)
{
    if (!s || child < 0 || child >= s->count) return false;
    if (parent >= s->count) return false;
    if (parent == child) return false;

    int walk = parent;
    for (int guard = 0; walk >= 0 && guard < SCENE_MAX_ENTITIES; guard++) {
        if (walk == child) return false;
        walk = s->items[walk].parent;
    }
    s->items[child].parent = parent;
    return true;
}

int SceneChildCount(const Scene *s, int parent)
{
    int n = 0;
    if (!s) return 0;
    for (int i = 0; i < s->count; i++)
        if (s->items[i].parent == parent) n++;
    return n;
}

int SceneChildAt(const Scene *s, int parent, int nth)
{
    if (!s || nth < 0) return -1;
    int seen = 0;
    for (int i = 0; i < s->count; i++) {
        if (s->items[i].parent != parent) continue;
        if (seen++ == nth) return i;
    }
    return -1;
}

static void world_trs(const Scene *s, int index, Vector2 *pos, float *rot,
                     Vector2 *scale, int depth)
{
    const Entity *e = SceneEntityConst(s, index);
    if (!e) {
        if (pos) *pos = (Vector2){ 0, 0 };
        if (rot) *rot = 0;
        if (scale) *scale = (Vector2){ 1, 1 };
        return;
    }

    Vector2 p = e->position;
    float r = e->rotation;
    Vector2 sc = e->scale;

    if (e->parent >= 0 && e->parent < s->count && depth < 32) {
        Vector2 pp;
        float pr;
        Vector2 ps;
        world_trs(s, e->parent, &pp, &pr, &ps, depth + 1);

        Vector2 local = { p.x * ps.x, p.y * ps.y };
        float rad = pr * DEG2RAD;
        float cs = cosf(rad), sn = sinf(rad);
        p.x = pp.x + local.x * cs - local.y * sn;
        p.y = pp.y + local.x * sn + local.y * cs;
        r += pr;
        sc.x *= ps.x;
        sc.y *= ps.y;
    }

    if (pos) *pos = p;
    if (rot) *rot = r;
    if (scale) *scale = sc;
}

Vector2 SceneWorldPosition(const Scene *s, int index)
{
    Vector2 p = { 0, 0 };
    world_trs(s, index, &p, NULL, NULL, 0);
    return p;
}

float SceneWorldRotation(const Scene *s, int index)
{
    float r = 0;
    world_trs(s, index, NULL, &r, NULL, 0);
    return r;
}

Vector2 SceneWorldScale(const Scene *s, int index)
{
    Vector2 sc = { 1, 1 };
    world_trs(s, index, NULL, NULL, &sc, 0);
    return sc;
}

void SceneDefault(Scene *s)
{
    SceneInit(s, "Arena");
    int arena = SceneAdd(s, "Arena");
    if (arena >= 0) {
        Entity *e = SceneEntity(s, arena);
        snprintf(e->tag, sizeof e->tag, "arena");
        e->scale = (Vector2){ 1200.0f, 600.0f };
        e->has_collider = false;
    }
    int spawn = SceneAdd(s, "Player Spawn");
    if (spawn >= 0) {
        Entity *e = SceneEntity(s, spawn);
        snprintf(e->tag, sizeof e->tag, "player_spawn");
        e->position = (Vector2){ 600.0f, 300.0f };
        e->sprite_size = (Vector2){ 32.0f, 32.0f };
    }
}

/* Scene strings are quoted; a literal quote inside a name would end the
   value early, so write it as an apostrophe. The editor's text fields can
   otherwise turn a perfectly good scene into one that will not load again. */
static void write_quoted(FILE *f, const char *text)
{
    fputc('"', f);
    for (; text && *text; text++) fputc(*text == '"' ? '\'' : *text, f);
    fputc('"', f);
}

static void write_entity(FILE *f, const Scene *s, int index)
{
    const Entity *e = &s->items[index];
    fprintf(f, "entity ");
    write_quoted(f, e->name);
    fprintf(f, "\n  tag ");
    write_quoted(f, e->tag);
    fprintf(f, "\n");
    fprintf(f, "  parent %d\n", e->parent);
    fprintf(f, "  position %.4f %.4f\n", e->position.x, e->position.y);
    fprintf(f, "  rotation %.4f\n", e->rotation);
    fprintf(f, "  scale %.4f %.4f\n", e->scale.x, e->scale.y);
    fprintf(f, "  visible %d\n", e->visible ? 1 : 0);
    fprintf(f, "  layer %d\n", e->layer);
    if (e->has_sprite && e->sprite[0]) {
        fprintf(f, "  sprite ");
        write_quoted(f, e->sprite);
        fprintf(f, "\n");
        fprintf(f, "  sprite_size %.4f %.4f\n", e->sprite_size.x, e->sprite_size.y);
    }
    fprintf(f, "  tint %d %d %d %d\n", e->tint.r, e->tint.g, e->tint.b, e->tint.a);
    if (e->has_collider)
        fprintf(f, "  collider %.4f %d\n", e->collider_radius, e->collider_trigger ? 1 : 0);
    fprintf(f, "end\n");
}

bool SceneSave(const Scene *s, const char *path)
{
    if (!s || !path) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    fprintf(f, "# %s scene\n", ENGINE_NAME);
    fprintf(f, "# Edit in the engine editor or by hand.\n\n");
    fprintf(f, "scene ");
    write_quoted(f, s->name);
    fprintf(f, "\n\n");
    for (int i = 0; i < s->count; i++) write_entity(f, s, i);
    fclose(f);
    return true;
}

static void set_error(char *err, int errSize, int line, const char *fmt, ...)
{
    if (!err || errSize <= 0) return;
    char detail[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    snprintf(err, (size_t)errSize, "line %d: %s", line, detail);
}

bool SceneLoad(Scene *s, const char *path, char *err, int errSize)
{
    if (!s || !path) return false;
    if (err && errSize > 0) err[0] = '\0';

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && errSize > 0) snprintf(err, (size_t)errSize, "cannot open %s", path);
        return false;
    }

    Scene tmp;
    SceneInit(&tmp, "Scene");
    int current = -1;
    int line_no = 0;
    bool saw_scene = false;
    bool ok = true;
    char raw[1024];

    while (ok && fgets(raw, sizeof raw, f)) {
        line_no++;
        char *line = trim(raw);
        if (!*line || *line == '#' || *line == ';') continue;

        if (strncmp(line, "scene", 5) == 0 && isspace((unsigned char)line[5])) {
            char name[SCENE_NAME_MAX];
            parse_quoted(line + 5, name, sizeof name);
            snprintf(tmp.name, sizeof tmp.name, "%s", name);
            saw_scene = true;
            current = -1;
            continue;
        }

        if (strncmp(line, "entity", 6) == 0 && isspace((unsigned char)line[6])) {
            if (tmp.count >= SCENE_MAX_ENTITIES) {
                set_error(err, errSize, line_no, "too many entities");
                ok = false;
                break;
            }
            char name[SCENE_NAME_MAX];
            parse_quoted(line + 6, name, sizeof name);
            current = SceneAdd(&tmp, name);
            if (current < 0) { ok = false; break; }
            continue;
        }

        if (strcmp(line, "end") == 0) { current = -1; continue; }

        if (current < 0) {
            set_error(err, errSize, line_no, "property outside an entity");
            ok = false;
            break;
        }

        char key[64];
        int used = 0;
        if (sscanf(line, "%63s%n", key, &used) < 1) continue;
        const char *value = line + used;
        while (*value && isspace((unsigned char)*value)) value++;
        Entity *e = &tmp.items[current];

        if (strcmp(key, "tag") == 0) {
            parse_quoted(value, e->tag, sizeof e->tag);
        } else if (strcmp(key, "parent") == 0) {
            if (!parse_int(value, &e->parent)) { set_error(err, errSize, line_no, "bad parent"); ok = false; }
        } else if (strcmp(key, "position") == 0) {
            if (sscanf(value, "%f %f", &e->position.x, &e->position.y) != 2) { set_error(err, errSize, line_no, "bad position"); ok = false; }
        } else if (strcmp(key, "rotation") == 0) {
            if (!parse_float(value, &e->rotation)) { set_error(err, errSize, line_no, "bad rotation"); ok = false; }
        } else if (strcmp(key, "scale") == 0) {
            if (sscanf(value, "%f %f", &e->scale.x, &e->scale.y) != 2) { set_error(err, errSize, line_no, "bad scale"); ok = false; }
        } else if (strcmp(key, "visible") == 0) {
            int v = 1;
            if (!parse_int(value, &v)) { set_error(err, errSize, line_no, "bad visible"); ok = false; }
            else e->visible = v != 0;
        } else if (strcmp(key, "layer") == 0) {
            if (!parse_int(value, &e->layer)) { set_error(err, errSize, line_no, "bad layer"); ok = false; }
        } else if (strcmp(key, "sprite") == 0) {
            parse_quoted(value, e->sprite, sizeof e->sprite);
            e->has_sprite = e->sprite[0] != '\0';
        } else if (strcmp(key, "sprite_size") == 0) {
            if (sscanf(value, "%f %f", &e->sprite_size.x, &e->sprite_size.y) != 2) { set_error(err, errSize, line_no, "bad sprite_size"); ok = false; }
        } else if (strcmp(key, "tint") == 0) {
            int r = 255, g = 255, b = 255, a = 255;
            if (sscanf(value, "%d %d %d %d", &r, &g, &b, &a) < 3) { set_error(err, errSize, line_no, "bad tint"); ok = false; }
            else { e->tint = (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a }; }
        } else if (strcmp(key, "collider") == 0) {
            int trigger = 0;
            if (sscanf(value, "%f %d", &e->collider_radius, &trigger) < 1) { set_error(err, errSize, line_no, "bad collider"); ok = false; }
            else { e->has_collider = true; e->collider_trigger = trigger != 0; }
        } else {
            set_error(err, errSize, line_no, "unknown key '%s'", key);
            ok = false;
        }
    }

    fclose(f);
    if (!ok) return false;
    if (!saw_scene && tmp.count == 0) {
        if (err && errSize > 0) snprintf(err, (size_t)errSize, "empty scene file");
        return false;
    }

    /* validate parent indices after every entity has been read */
    for (int i = 0; i < tmp.count; i++) {
        if (tmp.items[i].parent >= tmp.count) tmp.items[i].parent = -1;
        if (tmp.items[i].parent < -1) tmp.items[i].parent = -1;
    }
    *s = tmp;
    return true;
}
