#ifndef ENGINE_H
#define ENGINE_H

#include "raylib.h"
#include <stdbool.h>

/* BHCPP Engine - the runtime every client shares.

   This is the layer below the game and the editor: window and frame
   lifecycle, wall-clock and scaled time, resource location, logging and the
   engine's own audio device.  A client (game.exe or editor.exe) calls
   EngineInit(), loops on EngineBeginFrame(), and calls EngineShutdown().
   Nothing in here knows about bullet patterns, menus or player stats. */

#define ENGINE_NAME    "BHCPP Engine"
#define ENGINE_VERSION "1.0.0"

typedef enum {
    ENGINE_LOG_INFO = 0,
    ENGINE_LOG_WARN,
    ENGINE_LOG_ERROR,
} EngineLogLevel;

typedef struct {
    int width;
    int height;
    int min_width;
    int min_height;
    const char *title;
    bool resizable;
    bool vsync;
    int target_fps;
} EngineConfig;

/* A sane 1200x600, resizable, 60 fps window. */
EngineConfig EngineDefaultConfig(void);

/* Create the window and the audio device. Returns false (after reporting the
   reason) when the graphics device cannot start. Call before any client
   system that touches textures or sounds. */
bool EngineInit(const EngineConfig *cfg);

/* Close the audio device and the window. Unload client assets first. */
void EngineShutdown(void);

/* Start a frame. Returns false once the window was closed or EngineQuit() was
   requested. Updates EngineDeltaTime()/EngineTime(). */
bool EngineBeginFrame(void);
void EngineEndFrame(void);

/* Frame time in seconds, already clamped and multiplied by the time scale. */
float EngineDeltaTime(void);
/* Unscaled, clamped frame time. */
float EngineRawDeltaTime(void);
/* Seconds since EngineInit(), scaled. */
double EngineTime(void);
float EngineTimeScale(void);
void  EngineSetTimeScale(float scale);

bool EngineShouldClose(void);
void EngineRequestQuit(void);

/* Live window size (the window can be resized every frame). */
int EngineWidth(void);
int EngineHeight(void);

/* The executable's directory, with a trailing separator. */
const char *EngineAppDir(void);
/* The resource root that was found: <app>/resources, <app>/../resources, or
   ./resources. Never NULL, but it may not exist. */
const char *EngineResourceDir(void);

/* Resolve a path relative to the resource root. Searches next to the exe
   first, then one level up, then the working directory. Returns false when
   no candidate exists and leaves `out` empty. `out` is always a caller-owned
   buffer - no shared static formatting buffers, so resolving several paths
   in one expression is safe. */
bool EngineResolveResource(const char *relative, char *out, int outSize);

/* Resolve and create the directory that should hold writable user files.
   Used for generated scenes and saved settings. Returns false when no
   location could be created. */
bool EngineUserDir(char *out, int outSize);

const char *EngineVersion(void);

/* Log through raylib and through the optional sink (the editor console). */
void EngineLogInfo(const char *fmt, ...);
void EngineLogWarn(const char *fmt, ...);
void EngineLogError(const char *fmt, ...);
typedef void (*EngineLogSink)(int level, const char *message);
void EngineSetLogSink(EngineLogSink sink);

#endif
