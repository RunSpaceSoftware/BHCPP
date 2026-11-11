#include "pattern_script.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "bullet.h"        /* BASE_W, BASE_H, MARGIN for ctx */
#include "patterns_io.h"   /* AimName/OriginName/SideName */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Scripts are compiled once and kept as registry refs, so a shot costs one
   pcall instead of a parse. The built-in aims live in one table. */
#define MAX_SCRIPTS 32
#define AIMS_SCRIPT "aims"

typedef struct { char name[SCRIPT_NAME_MAX]; int ref; } ScriptEntry;

static lua_State *g_L = NULL;
static ScriptEntry g_scripts[MAX_SCRIPTS];
static int g_count = 0;
static int g_aims_ref = LUA_NOREF;
static char g_dir[4096] = { 0 };
static char g_last_err[256] = { 0 };   /* last message ScriptSpawn handed out */

/* Pattern scripts only compute bullets, so they get the pure libraries and
   nothing that touches the machine: no io, no os, no package/require, and the
   base library's dofile/loadfile are removed. That keeps a pattern pack
   somebody sends you from being able to read files or run programs. */
static void open_script_libs(lua_State *L)
{
    static const luaL_Reg libs[] = {
        { "_G", luaopen_base },
        { LUA_COLIBNAME, luaopen_coroutine },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
        { NULL, NULL }
    };
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
}

static lua_State *state(void)
{
    if (!g_L) {
        g_L = luaL_newstate();
        if (g_L) {
            open_script_libs(g_L);
            /* aims use math.random, so give every run a different sequence */
            char seed[64];
            snprintf(seed, sizeof seed, "math.randomseed(%lld)", (long long)time(NULL));
            if (luaL_dostring(g_L, seed) != LUA_OK) lua_pop(g_L, 1);
        }
    }
    return g_L;
}

/* resources/scripts next to the exe, then one level up, then the cwd. */
static const char *script_dir(void)
{
    if (g_dir[0]) return g_dir;
    const char *app = GetApplicationDirectory();
    const char *cand[3] = { "%sresources/scripts", "%s../resources/scripts", "resources/scripts" };
    char probe[4096];
    for (int i = 0; i < 3; i++) {
        snprintf(probe, sizeof probe, cand[i], (i == 2) ? "" : app);
        if (!DirectoryExists(probe)) continue;
        snprintf(g_dir, sizeof g_dir, "%s", probe);
        return g_dir;
    }
    snprintf(g_dir, sizeof g_dir, "resources/scripts");   /* best effort for the message */
    return g_dir;
}

/* Compiles <name>.lua and leaves its chunk result on the stack, or returns
   false with a message in err. */
static bool load_chunk(const char *name, char *err, int errSize)
{
    lua_State *L = state();
    if (!L) {
        snprintf(err, errSize, "could not start lua");
        return false;
    }

    char path[4400];
    snprintf(path, sizeof path, "%s/%s.lua", script_dir(), name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errSize, "cannot open %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)size + 1);
    if (!src) {
        fclose(f);
        snprintf(err, errSize, "out of memory reading %s", path);
        return false;
    }
    size_t got = fread(src, 1, (size_t)size, f);
    src[got] = '\0';
    fclose(f);

    if (luaL_loadbuffer(L, src, got, path) != LUA_OK) {
        snprintf(err, errSize, "%s: %s", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        free(src);
        return false;
    }
    free(src);

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        snprintf(err, errSize, "%s: %s", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

/* Compiles <name>.lua and returns its global spawn() as a registry ref. */
static int script_ref(const char *name, char *err, int errSize)
{
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_scripts[i].name, name) == 0) return g_scripts[i].ref;

    lua_State *L = state();
    if (!L) {
        snprintf(err, errSize, "could not start lua");
        return -1;
    }
    if (g_count >= MAX_SCRIPTS) {
        snprintf(err, errSize, "more than %d scripts loaded", MAX_SCRIPTS);
        return -1;
    }
    /* every script defines the global spawn(); clear it first so a script that
       forgets to define one is reported instead of running the previous
       script's spawn() */
    lua_pushnil(L);
    lua_setglobal(L, "spawn");
    if (!load_chunk(name, err, errSize)) return -1;

    lua_getglobal(L, "spawn");
    if (!lua_isfunction(L, -1)) {
        snprintf(err, errSize, "%s.lua does not define a spawn(ctx) function", name);
        lua_pop(L, 1);
        return -1;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    snprintf(g_scripts[g_count].name, SCRIPT_NAME_MAX, "%s", name);
    g_scripts[g_count].ref = ref;
    g_count++;
    return ref;
}

/* Compiles aims.lua and returns its global aims table as a registry ref. */
static int aims_ref(char *err, int errSize)
{
    if (g_aims_ref != LUA_NOREF) return g_aims_ref;

    lua_State *L = state();
    if (!L) {
        snprintf(err, errSize, "could not start lua");
        return LUA_NOREF;
    }
    if (!load_chunk(AIMS_SCRIPT, err, errSize)) return LUA_NOREF;

    lua_getglobal(L, "aims");
    if (!lua_istable(L, -1)) {
        snprintf(err, errSize, "%s.lua does not define an aims table", AIMS_SCRIPT);
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    g_aims_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return g_aims_ref;
}

static void set_num(lua_State *L, const char *key, lua_Number v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

static void set_int(lua_State *L, const char *key, int v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, key);
}

static void set_str(lua_State *L, const char *key, const char *v)
{
    lua_pushstring(L, v ? v : "");
    lua_setfield(L, -2, key);
}

static float get_num(lua_State *L, const char *key, float fallback)
{
    lua_getfield(L, -1, key);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return v;
}

/* Everything a spawn function may look at. Angles go out in degrees, matching
   the text format; the lua side converts to radians where it needs them. */
static void push_ctx(lua_State *L, const Pattern *p, int width, int height,
                     float player_x, float player_y, int shot, float spin_now)
{
    lua_createtable(L, 0, 32);
    set_int(L, "width", width);
    set_int(L, "height", height);
    set_int(L, "base_w", BASE_W);
    set_int(L, "base_h", BASE_H);
    set_int(L, "margin", MARGIN);
    set_num(L, "player_x", player_x);
    set_num(L, "player_y", player_y);
    set_int(L, "shot", shot);
    set_num(L, "spin", (double)(spin_now * RAD2DEG));

    set_str(L, "aim", AimName(p->aim));
    set_str(L, "origin", OriginName(p->origin));
    set_str(L, "side", SideName(p->side));

    set_num(L, "point_x", p->point.x);
    set_num(L, "point_y", p->point.y);
    set_int(L, "count", p->count);
    set_num(L, "spread", (double)(p->spread * RAD2DEG));
    set_num(L, "speed", p->speed);
    set_num(L, "delay", p->delay);
    set_int(L, "repeat", p->repeat);
    set_int(L, "damage", p->damage);
    set_num(L, "radius", p->radius);
    set_num(L, "gravity", p->gravity);
    set_num(L, "jitter", (double)(p->jitter * RAD2DEG));
    set_num(L, "angle", (double)(p->angle * RAD2DEG));
    lua_pushboolean(L, p->has_angle);
    lua_setfield(L, -2, "has_angle");
    set_int(L, "gap", p->gap);
    set_num(L, "spawn_radius", p->spawn_radius);
    set_num(L, "min_radius", p->min_radius);
    set_int(L, "segments", p->segments);
    set_num(L, "length", p->length);
}

/* Reads the table of bullets a spawn function returned. */
static int read_shots(lua_State *L, const Pattern *p, ShotSpec *out, int maxOut)
{
    if (!lua_istable(L, -1)) return 0;

    int n = (int)lua_rawlen(L, -1);
    if (n > maxOut) n = maxOut;
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (!lua_istable(L, -1)) {
            n = i;   /* a non-table entry ends the list */
            lua_pop(L, 1);
            break;
        }
        out[i].x = get_num(L, "x", 0.0f);
        out[i].y = get_num(L, "y", 0.0f);
        out[i].angle = get_num(L, "angle", 0.0f);
        out[i].speed = get_num(L, "speed", p->speed);
        lua_pop(L, 1);
    }
    return n;
}

static int spawn_impl(const Pattern *p, int width, int height, float player_x, float player_y,
                      int shot, float spin_now, ShotSpec *out, int maxOut, char *err, int errSize)
{
    if (!p) return 0;
    lua_State *L = state();
    if (!L) {
        snprintf(err, errSize, "could not start lua");
        return -1;
    }

    /* a named script owns the shot, otherwise the pattern's aim does */
    if (p->script[0]) {
        int ref = script_ref(p->script, err, errSize);
        if (ref < 0) return -1;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    } else {
        int ref = aims_ref(err, errSize);
        if (ref == LUA_NOREF) return -1;
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);          /* aims table */
        lua_getfield(L, -1, AimName(p->aim));            /* aims[name] */
        lua_remove(L, -2);
        if (!lua_isfunction(L, -1)) {
            snprintf(err, errSize, "aims.lua has no '%s'", AimName(p->aim));
            lua_pop(L, 1);
            return -1;
        }
    }

    push_ctx(L, p, width, height, player_x, player_y, shot, spin_now);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        snprintf(err, errSize, "%s: %s", p->script[0] ? p->script : AIMS_SCRIPT,
                 lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }

    int n = read_shots(L, p, out, maxOut);
    lua_pop(L, 1);
    return n;
}

int ScriptSpawn(const Pattern *p, int width, int height, float player_x, float player_y,
                int shot, float spin_now, ShotSpec *out, int maxOut, char *err, int errSize)
{
    int n = spawn_impl(p, width, height, player_x, player_y, shot, spin_now,
                       out, maxOut, err, errSize);
    if (n < 0) {
        /* A broken script would otherwise repeat its message on every shot.
           Hand each distinct message out once; ScriptReload() starts over. */
        if (strcmp(err, g_last_err) == 0) err[0] = '\0';
        else snprintf(g_last_err, sizeof g_last_err, "%s", err);
    }
    return n;
}

void ScriptReload(void)
{
    g_last_err[0] = '\0';
    if (!g_L) return;
    lua_close(g_L);
    g_L = NULL;
    g_count = 0;
    g_aims_ref = LUA_NOREF;
}

void ScriptShutdown(void)
{
    ScriptReload();
}
