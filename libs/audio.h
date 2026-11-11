#ifndef AUDIO_H
#define AUDIO_H

#include "raylib.h"
#include <stdbool.h>

/* Sound effects, one file per name, looked up in the same resources/ folder the
   bullet pngs come from. Missing files and machines without an output device
   are not fatal: the game just stays silent. */

/* Open the audio device. Call after InitWindow(); safe to call if the machine
   has no sound card. */
void AudioInit(void);

/* Play `name` (a file in resources/, e.g. "shoot.wav"); "" or a missing file
   plays nothing. The first call loads the file, later ones reuse it. */
void AudioPlay(const char *name);

/* Forget the loaded sounds so files dropped in since start-up are picked up
   (the F5 reload). Stop nothing - a sound already playing plays out. */
void AudioReload(void);

/* Unload the sounds and close the device. */
void AudioClose(void);

#endif
