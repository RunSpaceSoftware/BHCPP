#include "assets.h"
#include "engine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TEXTURES 96
#define MAX_SOUNDS   32
#define MAX_MUSIC    8
#define PATH_MAX_LEN 4096

typedef struct {
    char key[ASSET_NAME_MAX];
    Texture2D texture;
    int state;   /* 0 = untried, 1 = loaded, 2 = missing */
} TextureEntry;

typedef struct {
    char key[ASSET_NAME_MAX];
    Sound sound;
    int state;
} SoundEntry;

typedef struct {
    char key[ASSET_NAME_MAX];
    Music music;
    int state;
} MusicEntry;

static TextureEntry textures[MAX_TEXTURES];
static int texture_count = 0;
static SoundEntry sounds[MAX_SOUNDS];
static int sound_count = 0;
static MusicEntry music_cache[MAX_MUSIC];
static int music_count = 0;

static MusicEntry *current_music = NULL;
static float music_volume = 1.0f;

static bool has_extension(const char *name)
{
    if (!name) return false;
    const char *slash = strrchr(name, '/');
    const char *back = strrchr(name, '\\');
    const char *base = slash > back ? slash : back;
    const char *dot = strrchr(base ? base : name, '.');
    return dot != NULL;
}

static bool resolve_asset(const char *name, char *out, int outSize)
{
    if (!name || !*name) return false;
    if (EngineResolveResource(name, out, outSize)) return true;
    if (!has_extension(name)) {
        char with_ext[PATH_MAX_LEN];
        snprintf(with_ext, sizeof with_ext, "%s.png", name);
        return EngineResolveResource(with_ext, out, outSize);
    }
    return false;
}

bool AssetsExists(const char *name)
{
    char path[PATH_MAX_LEN];
    return resolve_asset(name, path, sizeof path);
}

char *AssetsReadText(const char *name)
{
    char path[PATH_MAX_LEN];
    if (!resolve_asset(name, path, sizeof path)) return NULL;
    return LoadFileText(path);
}

Texture2D AssetsTexture(const char *name)
{
    Texture2D empty = { 0 };
    if (!name || !*name) return empty;

    TextureEntry *e = NULL;
    for (int i = 0; i < texture_count; i++)
        if (strcmp(textures[i].key, name) == 0) { e = &textures[i]; break; }
    if (!e) {
        if (texture_count >= MAX_TEXTURES) return empty;
        e = &textures[texture_count++];
        snprintf(e->key, sizeof e->key, "%s", name);
    }
    if (e->state != 0) return e->texture;

    e->state = 2;
    char path[PATH_MAX_LEN];
    if (resolve_asset(name, path, sizeof path)) {
        e->texture = LoadTexture(path);
        if (e->texture.id != 0) {
            SetTextureFilter(e->texture, TEXTURE_FILTER_BILINEAR);
            e->state = 1;
        }
    }
    if (e->state == 2)
        EngineLogWarn("ASSET: texture '%s' not found", name);
    return e->texture;
}

Sound AssetsSound(const char *name)
{
    Sound empty = { 0 };
    if (!name || !*name || !IsAudioDeviceReady()) return empty;

    SoundEntry *e = NULL;
    for (int i = 0; i < sound_count; i++)
        if (strcmp(sounds[i].key, name) == 0) { e = &sounds[i]; break; }
    if (!e) {
        if (sound_count >= MAX_SOUNDS) return empty;
        e = &sounds[sound_count++];
        snprintf(e->key, sizeof e->key, "%s", name);
    }
    if (e->state != 0) return e->sound;

    e->state = 2;
    char path[PATH_MAX_LEN];
    if (resolve_asset(name, path, sizeof path)) {
        e->sound = LoadSound(path);
        if (IsSoundValid(e->sound)) e->state = 1;
    }
    if (e->state == 2) EngineLogWarn("ASSET: sound '%s' not found", name);
    return e->sound;
}

Music AssetsMusic(const char *name)
{
    Music empty = { 0 };
    if (!name || !*name || !IsAudioDeviceReady()) return empty;

    MusicEntry *e = NULL;
    for (int i = 0; i < music_count; i++)
        if (strcmp(music_cache[i].key, name) == 0) { e = &music_cache[i]; break; }
    if (!e) {
        if (music_count >= MAX_MUSIC) return empty;
        e = &music_cache[music_count++];
        snprintf(e->key, sizeof e->key, "%s", name);
    }
    if (e->state != 0) return e->music;

    e->state = 2;
    char path[PATH_MAX_LEN];
    if (resolve_asset(name, path, sizeof path)) {
        e->music = LoadMusicStream(path);
        if (IsMusicValid(e->music)) e->state = 1;
    }
    if (e->state == 2) EngineLogWarn("ASSET: music '%s' not found", name);
    return e->music;
}

void AssetsReloadTextures(void)
{
    for (int i = 0; i < texture_count; i++) {
        if (textures[i].state == 1 && textures[i].texture.id != 0)
            UnloadTexture(textures[i].texture);
        textures[i].texture = (Texture2D){ 0 };
        textures[i].state = 0;
    }
    texture_count = 0;
}

void AssetsReloadAudio(void)
{
    AssetsMusicStop();
    for (int i = 0; i < sound_count; i++) {
        if (sounds[i].state == 1 && IsSoundValid(sounds[i].sound))
            UnloadSound(sounds[i].sound);
        sounds[i].sound = (Sound){ 0 };
        sounds[i].state = 0;
    }
    sound_count = 0;
    for (int i = 0; i < music_count; i++) {
        if (music_cache[i].state == 1 && IsMusicValid(music_cache[i].music))
            UnloadMusicStream(music_cache[i].music);
        music_cache[i].music = (Music){ 0 };
        music_cache[i].state = 0;
    }
    music_count = 0;
}

void AssetsReloadAll(void)
{
    AssetsReloadTextures();
    AssetsReloadAudio();
}

void AssetsUnload(void) { AssetsReloadAll(); }

static int compare_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

void AssetsScan(const char *subdir, const char *extension, AssetNameList *out)
{
    if (!out) return;
    out->count = 0;

    char dir[PATH_MAX_LEN];
    if (subdir && *subdir) snprintf(dir, sizeof dir, "%s/%s", EngineResourceDir(), subdir);
    else snprintf(dir, sizeof dir, "%s", EngineResourceDir());
    if (!DirectoryExists(dir)) return;

    FilePathList files = LoadDirectoryFilesEx(dir, extension, false);
    for (unsigned int i = 0; i < files.count && out->count < ASSET_LIST_MAX; i++) {
        const char *base = GetFileName(files.paths[i]);
        snprintf(out->items[out->count++], ASSET_NAME_MAX, "%s", base);
    }
    UnloadDirectoryFiles(files);
    qsort(out->items, (size_t)out->count, ASSET_NAME_MAX, compare_names);
}

bool AssetsMusicPlay(const char *name, bool loop)
{
    MusicEntry *e = NULL;
    for (int i = 0; i < music_count; i++)
        if (strcmp(music_cache[i].key, name) == 0) { e = &music_cache[i]; break; }
    if (!e) {
        Music m = AssetsMusic(name);
        if (!IsMusicValid(m)) return false;
        e = &music_cache[music_count - 1];
    }
    if (!IsMusicValid(e->music)) return false;

    if (current_music != e) {
        if (current_music && IsMusicValid(current_music->music))
            StopMusicStream(current_music->music);
        current_music = e;
        PlayMusicStream(e->music);
    } else if (!IsMusicStreamPlaying(e->music)) {
        PlayMusicStream(e->music);
    }
    e->music.looping = loop;
    SetMusicVolume(e->music, music_volume);
    return true;
}

void AssetsMusicStop(void)
{
    if (current_music && IsMusicValid(current_music->music))
        StopMusicStream(current_music->music);
    current_music = NULL;
}

void AssetsMusicUpdate(void)
{
    if (current_music && IsMusicValid(current_music->music))
        UpdateMusicStream(current_music->music);
}

void AssetsMusicSetVolume(float volume)
{
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    music_volume = volume;
    if (current_music && IsMusicValid(current_music->music))
        SetMusicVolume(current_music->music, music_volume);
}

bool AssetsMusicIsPlaying(void)
{
    return current_music && IsMusicValid(current_music->music) &&
           IsMusicStreamPlaying(current_music->music);
}
