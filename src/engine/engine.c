#include "engine.h"
#include "assets.h"
#include "audio.h"
#include "input.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
/* <windows.h> cannot be included here: it declares Rectangle() and
   CloseWindow(), which collide with raylib's names. */
__declspec(dllimport) int __stdcall MessageBoxA(void *hwnd, const char *text,
                                                const char *caption, unsigned int type);
#define MB_OK        0x00000000u
#define MB_ICONERROR 0x00000010u
#endif

#define ENGINE_PATH_MAX 4096

static bool        g_initialized = false;
static bool        g_should_quit = false;
static bool        g_closing = false;
static float       g_dt = 0.0f;
static float       g_raw_dt = 0.0f;
static float       g_time_scale = 1.0f;
static double      g_time = 0.0;
static int         g_width = 1200;
static int         g_height = 600;
static char        g_app_dir[ENGINE_PATH_MAX];
static char        g_resource_dir[ENGINE_PATH_MAX];
static EngineLogSink g_sink = NULL;

static void engine_trace(int level, const char *text, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    vprintf(text, copy);
    printf("\n");
    fflush(stdout);
    va_end(copy);

    if (level != LOG_FATAL) return;

    char msg[1024];
    va_copy(copy, args);
    vsnprintf(msg, sizeof msg, text, copy);
    va_end(copy);
#if defined(_WIN32)
    MessageBoxA(NULL, msg, "BHCPP Engine - failed to start", MB_OK | MB_ICONERROR);
#else
    fprintf(stderr, "FATAL: %s\n", msg);
#endif
}

EngineConfig EngineDefaultConfig(void)
{
    EngineConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.width = 1200;
    cfg.height = 600;
    cfg.min_width = 800;
    cfg.min_height = 480;
    cfg.title = "BHCPP Engine";
    cfg.resizable = true;
    cfg.vsync = false;
    cfg.target_fps = 60;
    return cfg;
}

static void set_path(char *out, int size, const char *text)
{
    if (!out || size <= 0) return;
    snprintf(out, (size_t)size, "%s", text ? text : "");
}

static bool path_exists(const char *path)
{
    return FileExists(path) || DirectoryExists(path);
}

/* Append without a format string, so a long app path can only truncate the
   result - it can never trip the compiler's format-truncation warning. */
static void path_append(char *out, int out_size, const char *part)
{
    if (!out || out_size <= 0 || !part) return;
    size_t used = strlen(out);
    size_t add = strlen(part);
    if (used + add >= (size_t)out_size) add = (size_t)out_size - used - 1;
    memcpy(out + used, part, add);
    out[used + add] = '\0';
}

static void find_paths(void)
{
    const char *app = GetApplicationDirectory();
    if (!app) app = "";
    set_path(g_app_dir, sizeof g_app_dir, app);

    char candidate[ENGINE_PATH_MAX];
    candidate[0] = '\0';
    path_append(candidate, sizeof candidate, g_app_dir);
    path_append(candidate, sizeof candidate, "resources");
    if (DirectoryExists(candidate)) {
        set_path(g_resource_dir, sizeof g_resource_dir, candidate);
        return;
    }
    candidate[0] = '\0';
    path_append(candidate, sizeof candidate, g_app_dir);
    path_append(candidate, sizeof candidate, "../resources");
    if (DirectoryExists(candidate)) {
        set_path(g_resource_dir, sizeof g_resource_dir, candidate);
        return;
    }
    set_path(candidate, sizeof candidate, "resources");
    if (DirectoryExists(candidate)) {
        set_path(g_resource_dir, sizeof g_resource_dir, candidate);
        return;
    }
    /* No resource folder yet: the app-local path is where one will live. */
    candidate[0] = '\0';
    path_append(candidate, sizeof candidate, g_app_dir);
    path_append(candidate, sizeof candidate, "resources");
    set_path(g_resource_dir, sizeof g_resource_dir, candidate);
}

bool EngineInit(const EngineConfig *cfg)
{
    if (g_initialized) return true;

    EngineConfig defaults = EngineDefaultConfig();
    if (!cfg) cfg = &defaults;

    SetTraceLogCallback(engine_trace);
    SetTraceLogLevel(LOG_INFO);

    unsigned int flags = 0;
    if (cfg->resizable) flags |= FLAG_WINDOW_RESIZABLE;
    if (cfg->vsync) flags |= FLAG_VSYNC_HINT;
    flags |= FLAG_MSAA_4X_HINT;
    SetConfigFlags(flags);

    int w = cfg->width > 0 ? cfg->width : 1200;
    int h = cfg->height > 0 ? cfg->height : 600;
    InitWindow(w, h, cfg->title ? cfg->title : "BHCPP Engine");
    if (!IsWindowReady()) {
        TraceLog(LOG_FATAL, "ENGINE: the graphics device could not start");
        return false;
    }

    SetWindowMinSize(cfg->min_width > 0 ? cfg->min_width : 800,
                     cfg->min_height > 0 ? cfg->min_height : 480);
    SetExitKey(KEY_NULL);
    if (!cfg->vsync) SetTargetFPS(cfg->target_fps > 0 ? cfg->target_fps : 60);

    find_paths();
    InputReset();
    g_width = GetScreenWidth();
    g_height = GetScreenHeight();
    g_dt = g_raw_dt = 1.0f / 60.0f;
    g_time = 0.0;
    g_time_scale = 1.0f;
    g_should_quit = false;
    g_closing = false;
    g_initialized = true;

    /* The engine owns the output device; patterns and UI share it. */
    AudioInit();

    TraceLog(LOG_INFO, "%s %s - %s", ENGINE_NAME, ENGINE_VERSION, g_resource_dir);
    return true;
}

void EngineShutdown(void)
{
    if (!g_initialized) return;
    g_closing = true;
    AssetsUnload();
    AudioClose();
    CloseWindow();
    g_initialized = false;
    g_closing = false;
}

bool EngineBeginFrame(void)
{
    if (!g_initialized || g_closing) return false;
    if (g_should_quit || WindowShouldClose()) return false;

    g_raw_dt = GetFrameTime();
    if (!(g_raw_dt > 0.0f)) g_raw_dt = 1.0f / 60.0f;
    if (g_raw_dt > 0.1f) g_raw_dt = 0.1f;
    g_dt = g_raw_dt * g_time_scale;
    g_time += g_dt;
    g_width = GetScreenWidth();
    g_height = GetScreenHeight();
    return true;
}

void EngineEndFrame(void)
{
    /* kept as the matching half of EngineBeginFrame; raylib swaps buffers in
       EndDrawing(), which the client owns */
}

float EngineDeltaTime(void) { return g_dt; }
float EngineRawDeltaTime(void) { return g_raw_dt; }
double EngineTime(void) { return g_time; }
float EngineTimeScale(void) { return g_time_scale; }

void EngineSetTimeScale(float scale)
{
    if (!(scale >= 0.0f)) scale = 0.0f;
    if (scale > 8.0f) scale = 8.0f;
    g_time_scale = scale;
}

bool EngineShouldClose(void)
{
    return !g_initialized || g_closing || g_should_quit || WindowShouldClose();
}

void EngineRequestQuit(void) { g_should_quit = true; }
int EngineWidth(void) { return g_width; }
int EngineHeight(void) { return g_height; }
const char *EngineAppDir(void) { return g_app_dir; }
const char *EngineResourceDir(void) { return g_resource_dir; }
const char *EngineVersion(void) { return ENGINE_VERSION; }

bool EngineResolveResource(const char *relative, char *out, int outSize)
{
    if (out && outSize > 0) out[0] = '\0';
    if (!out || outSize <= 0) return false;
    if (!relative) return false;

    const char *rel = relative;
    while (*rel == '/' || *rel == '\\') rel++;
    if (strncmp(rel, "resources/", 10) == 0) rel += 10;

    char candidate[ENGINE_PATH_MAX];
    candidate[0] = '\0';
    path_append(candidate, sizeof candidate, g_resource_dir);
    path_append(candidate, sizeof candidate, "/");
    path_append(candidate, sizeof candidate, rel);
    if (path_exists(candidate)) { set_path(out, outSize, candidate); return true; }

    candidate[0] = '\0';
    path_append(candidate, sizeof candidate, g_app_dir);
    path_append(candidate, sizeof candidate, "../resources/");
    path_append(candidate, sizeof candidate, rel);
    if (path_exists(candidate)) { set_path(out, outSize, candidate); return true; }

    snprintf(candidate, sizeof candidate, "resources/%s", rel);
    if (path_exists(candidate)) { set_path(out, outSize, candidate); return true; }

    if (path_exists(relative)) { set_path(out, outSize, relative); return true; }
    return false;
}

bool EngineUserDir(char *out, int outSize)
{
    if (!out || outSize <= 0) return false;
    char candidate[ENGINE_PATH_MAX];
    candidate[0] = '\0';
    path_append(candidate, sizeof candidate, g_app_dir);
    path_append(candidate, sizeof candidate, "user");
    MakeDirectory(candidate);
    if (DirectoryExists(candidate)) { set_path(out, outSize, candidate); return true; }
    candidate[0] = '\0';
    path_append(candidate, sizeof candidate, g_resource_dir);
    path_append(candidate, sizeof candidate, "/generated");
    MakeDirectory(candidate);
    if (DirectoryExists(candidate)) { set_path(out, outSize, candidate); return true; }
    set_path(out, outSize, ".");
    return true;
}

static void engine_log(int level, const char *fmt, va_list args)
{
    char msg[2048];
    vsnprintf(msg, sizeof msg, fmt, args);

    int raylib_level = level == ENGINE_LOG_ERROR ? LOG_ERROR
                     : level == ENGINE_LOG_WARN  ? LOG_WARNING : LOG_INFO;
    TraceLog(raylib_level, "%s", msg);

    if (g_sink) g_sink(level, msg);
}

void EngineLogInfo(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    engine_log(ENGINE_LOG_INFO, fmt, args);
    va_end(args);
}

void EngineLogWarn(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    engine_log(ENGINE_LOG_WARN, fmt, args);
    va_end(args);
}

void EngineLogError(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    engine_log(ENGINE_LOG_ERROR, fmt, args);
    va_end(args);
}

void EngineSetLogSink(EngineLogSink sink) { g_sink = sink; }
