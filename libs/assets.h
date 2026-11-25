#ifndef ASSETS_H
#define ASSETS_H

#include "raylib.h"
#include <stdbool.h>

/* Central asset cache. Clients ask for a file under resources/ by name
   ("player.png", "scripts/aims.lua") and get a loaded texture/sound/music or
   a zero id when it is missing. Reload functions drop the cache so files
   added while the program is running are picked up (F5 in the editor/game). */

#define ASSET_NAME_MAX 128
#define ASSET_LIST_MAX 96

Texture2D AssetsTexture(const char *name);
Sound     AssetsSound(const char *name);
Music     AssetsMusic(const char *name);

/* True when resources/<name> exists (also checks the fallback roots). */
bool AssetsExists(const char *name);

/* Load a whole text file from resources/. Returns a raylib-owned buffer;
   release it with UnloadFileText(). NULL when the file is missing. */
char *AssetsReadText(const char *name);

void AssetsReloadTextures(void);
void AssetsReloadAudio(void);
void AssetsReloadAll(void);
void AssetsUnload(void);

typedef struct {
    char items[ASSET_LIST_MAX][ASSET_NAME_MAX];
    int count;
} AssetNameList;

/* List files with `extension` (e.g. ".png") under resources/<subdir>.
   `subdir` may be "" for the resource root. Names are base names, sorted. */
void AssetsScan(const char *subdir, const char *extension, AssetNameList *out);

/* One music track at a time - enough for menus and a run, and it keeps the
   update/stop rules in one place. */
bool AssetsMusicPlay(const char *name, bool loop);
void AssetsMusicStop(void);
void AssetsMusicUpdate(void);
void AssetsMusicSetVolume(float volume);
bool AssetsMusicIsPlaying(void);

#endif
