#include "pattern_script.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Scripts are compiled once and kept as registry refs, so a shot costs one
   pcall instead of a parse. */
#define MAX_SCRIPTS 32

typedef struct { char name[SCRIPT_NAME_MAX]; int ref; } ScriptEntry;

static lua_State *g_L = NULL;
static ScriptEntry g_scripts[MAX_SCRIPTS];
static int g_count = 0;
static char g_dir[4096] = { 0 };

static lua_State *state(void)
{
    if (!g_L) {
        g_L = luaL_newstate();
        if (g_L) luaL_openlibs(g_L);
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

/* Compiles <name>.lua and returns its registry ref, or -1. */
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

    char path[4400];
    snprintf(path, sizeof path, "%s/%s.lua", script_dir(), name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errSize, "cannot open %s", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = (char *)malloc((size_t)size + 1);
    if (!src) {
        fclose(f);
        snprintf(err, errSize, "out of memory reading %s", path);
        return -1;
    }
    size_t got = fread(src, 1, (size_t)size, f);
    src[got] = '\0';
    fclose(f);

    if (luaL_loadbuffer(L, src, got, path) != LUA_OK) {
        snprintf(err, errSize, "%s: %s", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        free(src);
        return -1;
    }
    free(src);

    /* run the chunk, then pick up the spawn() it defined as a global */
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        snprintf(err, errSize, "%s: %s", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    lua_getglobal(L, "spawn");
    if (!lua_isfunction(L, -1)) {
        snprintf(err, errSize, "%s does not define a spawn(ctx) function", path);
        lua_pop(L, 1);
        return -1;
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    snprintf(g_scripts[g_count].name, SCRIPT_NAME_MAX, "%s", name);
    g_scripts[g_count].ref = ref;
    g_count++;
    return ref;
}

static void set_num(lua_State *L, const char *key, lua_Number v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

static float get_num(lua_State *L, const char *key, float fallback)
{
    lua_getfield(L, -1, key);
    float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return v;
}

int ScriptSpawn(const Pattern *p, int width, int height, float player_x, float player_y,
                int shot, ShotSpec *out, int maxOut, char *err, int errSize)
{
    if (!p || !p->script[0]) return 0;
    lua_State *L = state();
    if (!L) {
        snprintf(err, errSize, "could not start lua");
        return -1;
    }

    int ref = script_ref(p->script, err, errSize);
    if (ref < 0) return -1;

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_createtable(L, 0, 10);
    set_num(L, "width", width);
    set_num(L, "height", height);
    set_num(L, "player_x", player_x);
    set_num(L, "player_y", player_y);
    set_num(L, "shot", shot);
    set_num(L, "count", p->count);
    set_num(L, "spread", p->spread * (180.0 / PI));
    set_num(L, "speed", p->speed);
    set_num(L, "radius", p->radius);
    set_num(L, "gravity", p->gravity);

    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        snprintf(err, errSize, "%s: %s", p->script, lua_tostring(L, -1));
        lua_pop(L, 1);
        return -1;
    }
    if (!lua_istable(L, -1)) {
        snprintf(err, errSize, "%s: spawn() must return a table of bullets", p->script);
        lua_pop(L, 1);
        return -1;
    }

    int n = (int)lua_rawlen(L, -1);
    if (n > maxOut) n = maxOut;
    for (int i = 0; i < n; i++) {
        lua_rawgeti(L, -1, i + 1);
        if (lua_istable(L, -1)) {
            ShotSpec s;
            s.x = get_num(L, "x", 0.0f);
            s.y = get_num(L, "y", 0.0f);
            s.angle = get_num(L, "angle", 0.0f);
            s.speed = get_num(L, "speed", p->speed);
            out[i] = s;
        } else {
            n = i;   /* a non-table entry ends the list */
            lua_pop(L, 1);
            break;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return n;
}

void ScriptShutdown(void)
{
    if (!g_L) return;
    lua_close(g_L);
    g_L = NULL;
    g_count = 0;
}
