#include "audio.h"
#include "patterns.h"   /* SOUND_NAME_MAX */
#include <stdio.h>
#include <string.h>

/* Sounds are cached by name exactly like bullet sprites, and loaded from the
   same folder: resources/ next to the exe, one level up, then the cwd.
   shortcut: 16 distinct sounds per session, raise MAX_SOUNDS when a table
   outgrows it (past that the extra names stay silent). */
#define MAX_SOUNDS 16

static char  names[MAX_SOUNDS][SOUND_NAME_MAX];
static Sound sounds[MAX_SOUNDS];
static bool  tried[MAX_SOUNDS];    /* tried once, so a missing file stays cheap */
static int   count = 0;
static bool  ready = false;        /* an output device exists */

static Sound *sound_for(const char *name)
{
    if (!ready || !name || !*name) return NULL;

    int id = -1;
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], name) == 0) { id = i; break; }
    if (id < 0) {
        if (count >= MAX_SOUNDS) return NULL;
        id = count++;
        snprintf(names[id], SOUND_NAME_MAX, "%s", name);
    }

    if (!tried[id]) {
        tried[id] = true;
        const char *app = GetApplicationDirectory();
        const char *fmt[3] = { "%sresources/%s", "%s../resources/%s", "%sresources/%s" };
        char path[4096];
        for (int i = 0; i < 3 && !IsSoundValid(sounds[id]); i++) {
            snprintf(path, sizeof path, fmt[i], (i == 2) ? "" : app, names[id]);
            if (!FileExists(path)) continue;
            sounds[id] = LoadSound(path);
        }
        if (!IsSoundValid(sounds[id]))
            TraceLog(LOG_WARNING, "AUDIO: %s not found in resources - playing nothing", names[id]);
    }
    return IsSoundValid(sounds[id]) ? &sounds[id] : NULL;
}

void AudioInit(void)
{
    InitAudioDevice();
    ready = IsAudioDeviceReady();
    if (!ready) TraceLog(LOG_WARNING, "AUDIO: no output device - sound disabled");
}

void AudioPlay(const char *name)
{
    Sound *s = sound_for(name);
    if (s) PlaySound(*s);
}

void AudioReload(void)
{
    if (!ready) return;
    for (int i = 0; i < count; i++) {
        if (IsSoundValid(sounds[i])) UnloadSound(sounds[i]);
        sounds[i] = (Sound){ 0 };
        tried[i] = false;
    }
    count = 0;
}

void AudioClose(void)
{
    if (!ready) return;
    AudioReload();
    CloseAudioDevice();
    ready = false;
}
