#include "player.h"
#include "playlist.h"
#include "mp3.h"       // for Mp3MetadataEntry
#include <SDL_mixer.h>
#include <stdio.h>
#include <switch.h>
#include <math.h>
#include <mpg123.h>

#define FFT_SIZE 1024

float g_fftInput[FFT_SIZE] = {0};
static int fftWritePos = 0;

static Mix_Music* currentMusic = nullptr;
static int currentTrackIndex = -1;
static bool musicFinished = false;
static u64 trackStartTick = 0;
static int currentTrackLength = 0;

// Master controls
static float g_volume   = 1.0f;   // 0.0 – 1.0
static float g_pan      = 0.0f;   // -1.0 left … +1.0 right
static float g_leftMix  = 1.0f;   // calculated output level
static float g_rightMix = 1.0f;

static void postmixCallback(void* udata, Uint8* stream, int len)
{
    int16_t* samples = (int16_t*)stream;
    int sampleCount = len / sizeof(int16_t);

    for (int i = 0; i < sampleCount; i += 2)
    {
        float left  = samples[i]     / 32768.0f;
        float right = samples[i + 1] / 32768.0f;

        // Apply volume & pan
        left  *= g_leftMix;
        right *= g_rightMix;

        // Clamp
        if (left  > 1.0f) left  = 1.0f;
        if (left  < -1.0f) left  = -1.0f;
        if (right > 1.0f) right = 1.0f;
        if (right < -1.0f) right = -1.0f;

        samples[i]     = (int16_t)(left  * 32767);
        samples[i + 1] = (int16_t)(right * 32767);

        g_fftInput[fftWritePos] = left;
        fftWritePos = (fftWritePos + 1) % FFT_SIZE;
    }
}

void playerApplyVolumePan()
{
    float left  = g_volume;
    float right = g_volume;

    if (g_pan < 0.0f)          // leaning LEFT
        right *= (1.0f + g_pan);
    else if (g_pan > 0.0f)     // leaning RIGHT
        left  *= (1.0f - g_pan);

    if (left  < 0.0f) left  = 0.0f;
    if (right < 0.0f) right = 0.0f;

    g_leftMix  = left;
    g_rightMix = right;

    // Base volume still controlled by SDL_mixer
    Mix_VolumeMusic((int)(g_volume * MIX_MAX_VOLUME));
}



void playerSetPan(float pan)
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    g_pan = pan;

    playerApplyVolumePan();   // update real audio output
}



float playerGetPan()
{
    return g_pan;
}

void playerSetVolume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    g_volume = v;
    playerApplyVolumePan();
    Mix_VolumeMusic((int)(v * MIX_MAX_VOLUME));
}

float playerGetVolume()
{
    return g_volume;
}

void playerAdjustVolume(float delta)
{
    playerSetVolume(g_volume + delta);
}


/* ---------- Callback when a song ends ---------- */
static void musicFinishedCallback()
{
    musicFinished = true;
}

int playerGetCurrentTrackIndex()
{
    return currentTrackIndex;
}

/* ---------- Init ---------- */
void playerInit()
{
    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096) < 0)
    {
      printf("Mix_OpenAudio error: %s\n", Mix_GetError());
    }

    mpg123_init();   // 🔥 REQUIRED — fixes random crashes

    Mix_SetPostMix(postmixCallback, NULL);

    Mix_HookMusicFinished(musicFinishedCallback);
    playerSetVolume(1.0f);
}

/* ---------- Play a track ---------- */
void playerPlay(int index)
{
    if (index < 0 || index >= playlistGetCount())
        return;

    const char* path = playlistGetTrack(index);
    if (!path) return;

    // Stop previous music safely
    Mix_HookMusicFinished(NULL);
    Mix_HaltMusic();

    if (currentMusic)
    {
        Mix_FreeMusic(currentMusic);
        currentMusic = nullptr;
    }

    currentMusic = Mix_LoadMUS(path);
    if (!currentMusic)
    {
        printf("Mix_LoadMUS failed: %s\n", Mix_GetError());
        return;
    }

    if (Mix_PlayMusic(currentMusic, 1) == -1)
    {
        printf("Mix_PlayMusic failed: %s\n", Mix_GetError());
        Mix_FreeMusic(currentMusic);
        currentMusic = nullptr;
        return;
    }

    Mix_HookMusicFinished(musicFinishedCallback);

    currentTrackIndex = index;
    musicFinished = false;
    trackStartTick = svcGetSystemTick();

    const Mp3MetadataEntry* md = mp3GetTrackMetadata(index);
    currentTrackLength = md ? md->durationSeconds : 0;
}



/* ---------- Stop playback ---------- */
void playerStop()
{
    Mix_HookMusicFinished(NULL);
    Mix_HaltMusic();

    if (currentMusic)
    {
        Mix_FreeMusic(currentMusic);
        currentMusic = NULL;
    }

    currentTrackIndex = -1;
    currentTrackLength = 0;
    trackStartTick = 0;
    musicFinished = false;
}





/* ---------- Auto-next handling ---------- */
void playerUpdate()
{
    if (!musicFinished)
        return;

    musicFinished = false;

    int next = currentTrackIndex + 1;
    if (next < playlistGetCount())
    {
        playerPlay(next);
    }
    else
    {
        playerStop();
    }
}


/* ---------- Status helpers ---------- */
bool playerIsPlaying()
{
    return Mix_PlayingMusic() != 0;
}

int playerGetCurrentIndex()
{
    return currentTrackIndex;
}

/* ---------- Elapsed / duration ---------- */
int playerGetElapsedSeconds()
{
    if (currentTrackIndex < 0 || trackStartTick == 0)
        return 0;

    u64 now = svcGetSystemTick();
    u64 diff = now - trackStartTick;

    // Switch system tick frequency = 19.2 MHz
    return diff / 19200000;
}

int playerGetTrackLength()
{
    return currentTrackLength;
}
