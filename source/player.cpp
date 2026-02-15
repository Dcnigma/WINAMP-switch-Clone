#include "player.h"
#include "player_state.h"
#include "playlist.h"
#include "mp3.h"

#include <SDL.h>
#include <stdio.h>
#include <switch.h>
#include <mpg123.h>
#include <string.h>

#define FFT_SIZE 1024
#define DECODE_BUFFER 8192

float g_fftInput[FFT_SIZE] = {0};
static int fftWritePos = 0;

static SDL_AudioDeviceID audioDev = 0;
static mpg123_handle* mh = nullptr;

static bool g_waitForDrain = false;


/* 🔵 WINAMP STATE */
PlayerState g_state = {
    .trackIndex = -1,
    .elapsedSeconds = 0,
    .durationSeconds = 0,
    .sampleRate = 0,
    .channels = 0,

    .playing = false,
    .paused  = false,

    .shuffle = false,
    .repeat  = REPEAT_OFF
};



/* 🔵 AUDIO TIME TRACKING */
static uint64_t samplesPlayed = 0;

/* MIXING */
static float g_volume   = 1.0f;
static float g_pan      = 0.0f;
static float g_leftMix  = 1.0f;
static float g_rightMix = 1.0f;


static void processSamples(int16_t* samples, int count)
{
    for (int i = 0; i < count; i += 2)
    {
        float left  = samples[i]     / 32768.0f;
        float right = samples[i + 1] / 32768.0f;

        left  *= g_leftMix;
        right *= g_rightMix;

        samples[i]     = (int16_t)(left  * 32767);
        samples[i + 1] = (int16_t)(right * 32767);

        g_fftInput[fftWritePos] = left;
        fftWritePos = (fftWritePos + 1) % FFT_SIZE;
    }
}


bool playerIsShuffleEnabled()
{
  return g_state.shuffle;
}

bool playerIsRepeatEnabled()
{
  return g_state.repeat != REPEAT_OFF;
}


/* ---------------------------------------------------- */
/* VOLUME + PAN                                         */
/* ---------------------------------------------------- */
void playerApplyVolumePan()
{
    float left  = g_volume;
    float right = g_volume;

    if (g_pan < 0.0f) right *= (1.0f + g_pan);
    else if (g_pan > 0.0f) left *= (1.0f - g_pan);

    if (left  < 0.0f) left  = 0.0f;
    if (right < 0.0f) right = 0.0f;

    g_leftMix  = left;
    g_rightMix = right;
}

void playerSetPan(float pan)
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    g_pan = pan;
    playerApplyVolumePan();
}

float playerGetPan() { return g_pan; }

void playerSetVolume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    g_volume = v;
    playerApplyVolumePan();
}

float playerGetVolume() { return g_volume; }
void playerAdjustVolume(float delta) { playerSetVolume(g_volume + delta); }

void playerInit()
{
    mpg123_init();
    playerSetVolume(1.0f);

    g_state.repeat  = REPEAT_OFF;
    g_state.shuffle = false;
    g_state.paused = false;

}

void playerShutdown()
{
    playerStop();

    if (audioDev)
    {
        SDL_CloseAudioDevice(audioDev);
        audioDev = 0;
    }

    mpg123_exit();
}

// void playerNext()
// {
//     int next;
//
//     if (g_state.repeat == REPEAT_ONE)
//     {
//         next = g_state.trackIndex;
//     }
//     else if (g_state.shuffle)
//     {
//         next = rand() % playlistGetCount();
//     }
//     else
//     {
//         next = g_state.trackIndex + 1;
//         if (next >= playlistGetCount())
//         {
//             if (g_state.repeat == REPEAT_ALL)
//                 next = 0;
//             else
//                 return;
//         }
//     }
//
//     playerPlay(next);
// }

void playerNext()
{
    int count = playlistGetCount();
    if (count == 0)
        return;

    int next = g_state.trackIndex;

    if (g_state.repeat == REPEAT_ONE)
    {
        // same track
    }
    else if (g_state.shuffle)
    {
        next = rand() % count;
    }
    else
    {
        next++;
        if (next >= count)
        {
            if (g_state.repeat == REPEAT_ALL)
                next = 0;
            else
            {
                playerStop();
                return;
            }
        }
    }

    playerPlay(next);
}


void playerTogglePause()
{
    if (!g_state.playing)
        return;

    g_state.paused = !g_state.paused;

    if (g_state.paused)
        SDL_PauseAudioDevice(audioDev, 1);
    else
        SDL_PauseAudioDevice(audioDev, 0);
}


void playerToggleShuffle()
{
    g_state.shuffle = !g_state.shuffle;
    //g_shuffleEnabled = !g_shuffleEnabled;
}

void playerCycleRepeat()
{
    g_state.repeat = (RepeatMode)((g_state.repeat + 1) % 3);
}

bool playerIsPaused()
{
    return g_state.playing && g_state.paused;
}



// void playerPrev()
// {
//     // Winamp rule: if >2 sec played → restart track
//     if (g_state.elapsedSeconds > 2)
//     {
//         playerPlay(g_state.trackIndex);
//         return;
//     }
//
//     int prev = g_state.trackIndex - 1;
//
//     if (prev < 0)
//     {
//         if (g_state.repeat == REPEAT_ALL)
//             prev = playlistGetCount() - 1;
//         else
//             return;
//     }
//
//     playerPlay(prev);
// }

void playerPrev()
{
    int count = playlistGetCount();
    if (count == 0)
        return;

    // Winamp rule: restart if >2s
    if (g_state.elapsedSeconds > 2)
    {
        playerPlay(g_state.trackIndex);
        return;
    }

    int prev = g_state.trackIndex - 1;

    if (prev < 0)
    {
        if (g_state.repeat == REPEAT_ALL)
            prev = count - 1;
        else
            return;
    }

    playerPlay(prev);
}



void playerToggleRepeat()
{
    switch (g_state.repeat)
    {
        case REPEAT_OFF:
            g_state.repeat = REPEAT_ALL;
            break;

        case REPEAT_ALL:
            g_state.repeat = REPEAT_ONE;
            break;

        case REPEAT_ONE:
        default:
            g_state.repeat = REPEAT_OFF;
            break;
    }
}





void playerPlay(int index)
{
    if (index < 0 || index >= playlistGetCount())
        return;

//    currentTrackIndex = index;

    const char* path = playlistGetTrack(index);
    if (!path)
        return;

    playerStop(); // 🔵 Winamp always stops first
    g_waitForDrain = false;

    mh = mpg123_new(NULL, NULL);
    mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_SKIP_ID3V2, 0);
    mpg123_param(mh, MPG123_FORCE_STEREO, 1, 0);

    if (mpg123_open(mh, path) != MPG123_OK)
    {
        mpg123_delete(mh);
        mh = nullptr;
        return;
    }

    long rate;
    int ch, enc;
    mpg123_getformat(mh, &rate, &ch, &enc);

    mpg123_format_none(mh);
    mpg123_format(mh, rate, ch, MPG123_ENC_SIGNED_16);

    /* 🔵 OPEN SDL WITH MATCHING FORMAT */
    SDL_AudioSpec want{}, have{};
    want.freq = rate;
    want.format = AUDIO_S16SYS;
    want.channels = ch;
    want.samples = 4096;

    audioDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audioDev)
    {
        mpg123_close(mh);
        mpg123_delete(mh);
        mh = nullptr;
        return;
    }

    SDL_PauseAudioDevice(audioDev, 0);

    /* 🔵 SET PLAYER STATE */
    g_state.trackIndex = index;
    g_state.sampleRate = rate;
    g_state.channels   = ch;
    g_state.playing    = true;
    g_state.paused     = false;


    samplesPlayed = 0;

    off_t len = mpg123_length(mh);
    g_state.durationSeconds =
        (len > 0) ? (int)(len / rate) : 0;
}

void playerStop()
{
    if (audioDev)
    {
        SDL_ClearQueuedAudio(audioDev);
        SDL_CloseAudioDevice(audioDev);
        audioDev = 0;
    }

    if (mh)
    {
        mpg123_close(mh);
        mpg123_delete(mh);
        mh = nullptr;
    }

    memset(&g_state, 0, sizeof(g_state));
    g_state.trackIndex = -1;

    g_waitForDrain = false;

    samplesPlayed = 0;

    g_state.playing = false;
    g_state.paused  = false;

}

void playerUpdate()
{
    if (!mh || !audioDev)
        return;

    // 🔵 DRAIN PHASE (runs after decoding ends)
    if (g_waitForDrain)
    {
        if (SDL_GetQueuedAudioSize(audioDev) == 0)
        {
            g_waitForDrain = false;

            int next;

            if (g_state.repeat == REPEAT_ONE)
            {
                next = g_state.trackIndex;
            }
            else if (g_state.shuffle)
            {
                next = rand() % playlistGetCount();
            }
            else
            {
                next = g_state.trackIndex + 1;
                if (next >= playlistGetCount())
                {
                    if (g_state.repeat == REPEAT_ALL)
                        next = 0;
                    else
                    {
                        playerStop();
                        return;
                    }
                }
            }

            if (g_state.playing && !g_state.paused)
            {
                playerNext();
            }

            return;
        }
    }

    // 🔵 DECODING PHASE
    if (!g_state.playing || g_state.paused)
        return;


    if (SDL_GetQueuedAudioSize(audioDev) > 48000 * 4)
        return;

    unsigned char buffer[DECODE_BUFFER];
    size_t done = 0;

    int err = mpg123_read(mh, buffer, sizeof(buffer), &done);

    if (done > 0)
    {
        int bytesPerSample = sizeof(int16_t) * g_state.channels;
        samplesPlayed += done / bytesPerSample;

        g_state.elapsedSeconds =
            (int)(samplesPlayed / g_state.sampleRate);

        processSamples((int16_t*)buffer, done / 2);
        SDL_QueueAudio(audioDev, buffer, done);
    }

    if (err == MPG123_DONE)
    {
//        g_state.isDecoding = false;
        g_waitForDrain = true;   // 🔑 transition to drain
    }
}


const PlayerState* playerGetState()
{
    return &g_state;
}

bool playerIsPlaying()
{
    return g_state.playing;
}

int playerGetCurrentTrackIndex()
{
    return g_state.trackIndex;
}

int playerGetElapsedSeconds()
{
    return g_state.elapsedSeconds;
}

int playerGetTrackLength()
{
    return g_state.durationSeconds;
}
