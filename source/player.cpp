#include "player.h"
#include "playlist.h"
#include "mp3.h"       // for Mp3MetadataEntry
#include <SDL_mixer.h>
#include <stdio.h>
#include <switch.h>    // ✅ needed for u64 and svcGetSystemTick
#include <math.h>

#define FFT_SIZE 1024

float g_fftInput[FFT_SIZE] = {0};   // waveform input
static int fftWritePos = 0;         // rolling write index

static Mix_Music* currentMusic = nullptr;
static int currentTrackIndex = -1;
static bool musicFinished = false;
static u64 trackStartTick = 0;   // Switch system tick
static int currentTrackLength = 0; // seconds


static void postmixCallback(void* udata, Uint8* stream, int len)
{
    int16_t* samples = (int16_t*)stream;
    int sampleCount = len / sizeof(int16_t);

    for (int i = 0; i < sampleCount; i += 2) // stereo → take left channel
    {
        g_fftInput[fftWritePos] = samples[i] / 32768.0f; // -1..1 float
        fftWritePos = (fftWritePos + 1) % FFT_SIZE;
    }
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

    Mix_SetPostMix(postmixCallback, NULL);

    Mix_HookMusicFinished(musicFinishedCallback);
}

/* ---------- Play a track ---------- */
void playerPlay(int index)
{
    if (index < 0 || index >= playlistGetCount())
        return;

    const char* path = playlistGetTrack(index);
    if (!path) return;

    if (currentMusic)
    {
        Mix_HaltMusic();
        Mix_FreeMusic(currentMusic);
    }

    currentMusic = Mix_LoadMUS(path);
    if (!currentMusic) return;

    Mix_PlayMusic(currentMusic, 1);

    currentTrackIndex = index;
    musicFinished = false;

    trackStartTick = svcGetSystemTick(); // ✅ start time

    const Mp3MetadataEntry* md = mp3GetTrackMetadata(index);
    currentTrackLength = md ? md->durationSeconds : 0;
}

/* ---------- Stop playback ---------- */
void playerStop()
{
    Mix_HaltMusic();

    if (currentMusic)
    {
        Mix_FreeMusic(currentMusic);
        currentMusic = nullptr;
    }

    currentTrackIndex = -1;
    currentTrackLength = 0;
    trackStartTick = 0;
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
