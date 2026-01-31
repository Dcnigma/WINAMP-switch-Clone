#include "player.h"
#include "playlist.h"
#include <SDL_mixer.h>
#include <stdio.h>

static Mix_Music* currentMusic = nullptr;
static int currentTrackIndex = -1;
static bool musicFinished = false;

/* ---------- Callback when a song ends ---------- */
void musicFinishedCallback()
{
    musicFinished = true;
}

/* ---------- Init ---------- */
void playerInit()
{
    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096) < 0)
    {
        printf("Mix_OpenAudio error: %s\n", Mix_GetError());
    }

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
        currentMusic = nullptr;
    }

    currentMusic = Mix_LoadMUS(path);
    if (!currentMusic)
    {
        printf("Failed to load MP3: %s\n", Mix_GetError());
        return;
    }

    Mix_PlayMusic(currentMusic, 1);

    currentTrackIndex = index;
    musicFinished = false;
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
        playerStop(); // end of playlist
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
