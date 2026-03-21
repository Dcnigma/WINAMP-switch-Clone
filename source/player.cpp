#include "player.h"
#include "player_state.h"
#include "playlist.h"
#include "mp3.h"
#include "audio_engine.h"
#include "ui.h"
#include <SDL.h>
#include <switch.h>
#include <mpg123.h>
#include <string.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <random>

#include "settings.h"
#include "settings_state.h"


/* ---------------------------------------------------- */
/* CONFIG                                               */
/* ---------------------------------------------------- */
#define FFT_SIZE 1024
#define DECODE_BUFFER 8192   // bytes (mpg123 output)

/* ---------------------------------------------------- */
/* GLOBALS                                              */
/* ---------------------------------------------------- */
static AudioEngine audio;
static mpg123_handle* mh = nullptr;
static std::vector<int> g_playQueue;
static bool g_waitForDrain = false;
static bool g_metadataSwitched = false;
static int g_crossfadeTargetIndex = -1;

enum PlaybackState
{
    STATE_STOPPED,
    STATE_PLAYING,
    STATE_CROSSFADING,
    STATE_DRAINING
};

static PlaybackState g_playbackState = STATE_STOPPED;

/* Forward declarations */
/*shuffle*/

static void stopPlaybackInternal();
static void rebuildShufflePool();
static std::vector<int> g_shufflePool;
static std::vector<int> g_shuffleHistory;

/* FFT */
float g_fftInput[FFT_SIZE] = {0};
static int fftWritePos = 0;

/* AUDIO TIME TRACKING */
static uint64_t samplesPlayed = 0;

/* MIXING */
static float g_volume   = 1.0f;
static float g_pan      = 0.0f;
static float g_leftMix  = 1.0f;
static float g_rightMix = 1.0f;

/* Crossfader*/

static mpg123_handle* mh_next = nullptr;

static bool g_crossfading = false;
static float g_crossfadeProgress = 0.0f;


static void playerCommitNextTrack(int nextIndex)
{
    if (nextIndex < 0)
    {
        playerStop();
        return;
    }

    if (!g_playQueue.empty())
    {
        g_playQueue.erase(g_playQueue.begin());
    }
    else if (g_state.shuffle && !g_shufflePool.empty())
    {
        g_shuffleHistory.push_back(g_state.trackIndex);
        g_shufflePool.pop_back();
    }

    g_state.trackIndex = nextIndex;
    playlistSetCurrentIndex(nextIndex);
}

static bool openNextDecoder(int index)
{
    const char* path = playlistGetTrack(index);
    if (!path)
        return false;

    mh_next = mpg123_new(nullptr, nullptr);
    if (!mh_next)
        return false;

    mpg123_param(mh_next, MPG123_ADD_FLAGS, MPG123_SKIP_ID3V2, 0);
    mpg123_param(mh_next, MPG123_FORCE_STEREO, 1, 0);

    if (mpg123_open(mh_next, path) != MPG123_OK)
        return false;

    long rate;
    int ch, enc;

    if (mpg123_getformat(mh_next, &rate, &ch, &enc) != MPG123_OK)
        return false;

    mpg123_format_none(mh_next);
    mpg123_format(mh_next, rate, ch, MPG123_ENC_SIGNED_16);

    return true;
}

void playerEnqueue(int index)
{
    if (index < 0 || index >= playlistGetCount())
        return;

    g_playQueue.push_back(index);
}

static void rebuildShufflePool()
{
    g_shufflePool.clear();

    int count = playlistGetCount();
    for (int i = 0; i < count; ++i)
        g_shufflePool.push_back(i);

    static std::mt19937 rng{ std::random_device{}() };
    std::shuffle(g_shufflePool.begin(), g_shufflePool.end(), rng);
}

static int playerPeekNextIndex()
{
    int count = playlistGetCount();
    if (count == 0)
        return -1;

    // QUEUE FIRST
    if (!g_playQueue.empty())
        return g_playQueue.front();

    // REPEAT ONE
    if (g_state.repeat == REPEAT_ONE && g_state.trackIndex >= 0)
        return g_state.trackIndex;

    // SHUFFLE
    if (g_state.shuffle)
    {
        if (!g_shufflePool.empty())
            return g_shufflePool.back();

        if (g_state.repeat == REPEAT_ALL)
        {
            rebuildShufflePool();

            auto it = std::find(
                g_shufflePool.begin(),
                g_shufflePool.end(),
                g_state.trackIndex
            );
            if (it != g_shufflePool.end())
                g_shufflePool.erase(it);

            if (!g_shufflePool.empty())
                return g_shufflePool.back();
        }

        return -1;
    }

    // NORMAL
    int next = g_state.trackIndex + 1;

    if (next >= count)
    {
        if (g_state.repeat == REPEAT_ALL)
            return 0;
        else
            return -1;
    }

    return next;
}
/* ---------------------------------------------------- */
/* PLAYER STATE                                         */
/* ---------------------------------------------------- */
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

/* ---------------------------------------------------- */
/* DSP                                                  */
/* ---------------------------------------------------- */
static void processSamplesToFloat(
    const int16_t* in,
    float* out,
    int frames,
    int channels
) {
    for (int i = 0; i < frames; i++) {
        float left  = in[i * channels]     / 32768.0f;
        float right = (channels > 1)
            ? in[i * channels + 1] / 32768.0f
            : left;

        left  *= g_leftMix;
        right *= g_rightMix;

        out[i * 2]     = left;
        out[i * 2 + 1] = right;

        g_fftInput[fftWritePos] = left;
        fftWritePos = (fftWritePos + 1) % FFT_SIZE;
    }
}

/* ---------------------------------------------------- */
/* VOLUME / PAN                                         */
/* ---------------------------------------------------- */
void playerAdjustVolume(float delta)
{
    playerSetVolume(g_volume + delta);
}

void playerApplyVolumePan()
{
    float left  = g_volume;
    float right = g_volume;

    if (g_pan < 0.0f) right *= (1.0f + g_pan);
    else if (g_pan > 0.0f) left *= (1.0f - g_pan);

    g_leftMix  = (left  < 0.0f) ? 0.0f : left;
    g_rightMix = (right < 0.0f) ? 0.0f : right;
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

/* ---------------------------------------------------- */
/* LIFECYCLE                                           */
/* ---------------------------------------------------- */
void playerInit()
{
    mpg123_init();
    playerSetVolume(1.0f);

    g_state.repeat  = REPEAT_OFF;
    g_state.shuffle = false;
    g_state.paused  = false;
}

void playerShutdown()
{
    playerStop();
    mpg123_exit();
}

/* ---------------------------------------------------- */
/* PLAYBACK CONTROL                                     */
/* ---------------------------------------------------- */
void playerPlay(int index)
{

    if (index < 0 || index >= playlistGetCount())
        return;

    const char* path = playlistGetTrack(index);
    if (!path)
        return;

    stopPlaybackInternal();
    g_waitForDrain = false;

    mh = mpg123_new(nullptr, nullptr);
    if (!mh)
        return;

    auto cleanup = [&]() {
        if (mh) {
            mpg123_close(mh);
            mpg123_delete(mh);
            mh = nullptr;
        }
        audio.shutdown();
    };

    mpg123_param(mh, MPG123_ADD_FLAGS, MPG123_SKIP_ID3V2, 0);
    mpg123_param(mh, MPG123_FORCE_STEREO, 1, 0);

    if (mpg123_open(mh, path) != MPG123_OK) {
        cleanup();
        return;
    }

    long rate = 0;
    int ch = 0, enc = 0;
    if (mpg123_getformat(mh, &rate, &ch, &enc) != MPG123_OK) {
        cleanup();
        return;
    }

    mpg123_format_none(mh);
    mpg123_format(mh, rate, ch, MPG123_ENC_SIGNED_16);

    if (!audio.init(rate, 2)) {
        cleanup();
        return;
    }

    audio.setPaused(false);
    audio.start();

    g_state.trackIndex = index;
    g_state.sampleRate = rate;
    g_state.channels   = ch;
    g_state.playing    = true;
    g_state.paused     = false;

    g_crossfading = false;

    g_waitForDrain = false;

    playlistSetCurrentIndex(index);
    off_t len = mpg123_length(mh);
    g_state.durationSeconds =
        (len > 0) ? (int)(len / g_state.sampleRate) : 0;

    samplesPlayed = 0;
    g_state.elapsedSeconds = 0;
    g_playbackState = STATE_PLAYING;
}

void playerStartCrossfade()
{
    if (!g_settings.crossfadeEnabled)
    {
        playerNext();
        return;
    }

//    int nextIndex = playerPeekNextIndex();
      g_crossfadeTargetIndex = playerPeekNextIndex();
      //int nextIndex = g_crossfadeTargetIndex;
      int nextIndex = (g_crossfadeTargetIndex >= 0)
        ? g_crossfadeTargetIndex
        : playerPeekNextIndex();
    if (nextIndex < 0)
        return;

    if (!openNextDecoder(nextIndex))
    {
        playerNext();
        return;
    }

    g_crossfading = true;
    g_playbackState = STATE_CROSSFADING;
    g_crossfadeProgress = 0.0f;
    g_metadataSwitched = false;   // 🔥 IMPORTANT
}

void playerStop()
{
    stopPlaybackInternal();
    spectrumReset();

    g_shuffleHistory.clear();
    g_shufflePool.clear();

    g_state.playing = false;
    g_state.paused  = false;
    g_state.trackIndex = -1;

    g_state.elapsedSeconds = 0;
    g_state.durationSeconds = 0;
    g_playbackState = STATE_STOPPED;
}
void playerTogglePause()
{
    if (!g_state.playing)
        return;

    g_state.paused = !g_state.paused;
    audio.setPaused(g_state.paused);
}

bool playerIsShuffleEnabled()
{
    return g_state.shuffle;
}

void playerToggleShuffle()
{
    g_state.shuffle = !g_state.shuffle;

    if (g_state.shuffle)
    {
        rebuildShufflePool();
        g_shuffleHistory.clear();

        auto it = std::find(
            g_shufflePool.begin(),
            g_shufflePool.end(),
            g_state.trackIndex
        );
        if (it != g_shufflePool.end())
            g_shufflePool.erase(it);
    }
    else
    {
        g_shufflePool.clear();
        g_shuffleHistory.clear();
    }
}

static void stopPlaybackInternal()
{
    audio.stop();
    audio.shutdown();

    if (mh) {
        mpg123_close(mh);
        mpg123_delete(mh);
        mh = nullptr;
    }

    samplesPlayed = 0;
    g_waitForDrain = false;
}

RepeatMode playerGetRepeatMode()
{
    return g_state.repeat;
}

bool playerIsRepeatEnabled()
{
    return g_state.repeat != REPEAT_OFF;
}



void playerCycleRepeat()
{
    switch (g_state.repeat)
    {
        case REPEAT_OFF: g_state.repeat = REPEAT_ALL; break;
        case REPEAT_ALL: g_state.repeat = REPEAT_ONE; break;
        case REPEAT_ONE: g_state.repeat = REPEAT_OFF; break;
    }
}



float playerGetPosition()
{
    if (!g_state.playing)
        return 0.0f;

    return (float)g_state.elapsedSeconds;
}

void playerSeek(float targetSeconds)
{
    if (!mh || !g_state.playing)
        return;

    if (targetSeconds < 0.0f)
        targetSeconds = 0.0f;

    if (targetSeconds > g_state.durationSeconds)
        targetSeconds = (float)g_state.durationSeconds;

    off_t targetSample =
        (off_t)(targetSeconds * g_state.sampleRate);

    // Stop audio output while seeking
    audio.setPaused(true);

    if (mpg123_seek(mh, targetSample, SEEK_SET) >= 0) {
        samplesPlayed = targetSample;
        g_state.elapsedSeconds = (int)targetSeconds;
    }

    audio.setPaused(g_state.paused);
    g_crossfading = false;
    g_waitForDrain = false;
    g_crossfadeTargetIndex = -1;
    g_metadataSwitched = false;
    if (mh_next)
    {
        mpg123_close(mh_next);
        mpg123_delete(mh_next);
        mh_next = nullptr;
    }
}

void playerNext()
{
    int count = playlistGetCount();
    if (count == 0)
        return;

    // QUEUE HAS PRIORITY
    if (!g_playQueue.empty())
    {
        int next = g_playQueue.front();
        g_playQueue.erase(g_playQueue.begin());
        playerPlay(next);
        return;
    }

    // REPEAT ONE → immediately restart same track
    if (g_state.repeat == REPEAT_ONE && g_state.trackIndex >= 0)
    {
        playerPlay(g_state.trackIndex);
        return;
    }

    int next = g_state.trackIndex;

    if (g_state.shuffle)
    {
        if (g_state.trackIndex >= 0)
            g_shuffleHistory.push_back(g_state.trackIndex);

        if (g_shufflePool.empty())
        {
            if (g_state.repeat == REPEAT_ALL)
            {
                rebuildShufflePool();

                // Prevent immediate repeat of current track
                auto it = std::find(
                    g_shufflePool.begin(),
                    g_shufflePool.end(),
                    g_state.trackIndex
                );
                if (it != g_shufflePool.end())
                    g_shufflePool.erase(it);
            }
            else
            {
                playerStop();
                return;
            }
        }

        next = g_shufflePool.back();
        g_shufflePool.pop_back();
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
    printf("HISTORY: ");
    for (int i : g_shuffleHistory)
        printf("%d ", i);
    printf("\n");
    playerPlay(next);
}

void playerPrev()
{
    if (g_state.shuffle)
    {
        if (!g_shuffleHistory.empty())
        {
            int prev = g_shuffleHistory.back();
            g_shuffleHistory.pop_back();
            playerPlay(prev);
        }
        else
        {
            // Restart current track
            playerPlay(g_state.trackIndex);
        }
        return;
    }

    int prev = g_state.trackIndex - 1;
    if (prev < 0)
        prev = playlistGetCount() - 1;
    printf("HISTORY: ");
    for (int i : g_shuffleHistory)
        printf("%d ", i);
    printf("\n");
    playerPlay(prev);
}

/* ---------------------------------------------------- */
/* UPDATE LOOP                                          */
/* ---------------------------------------------------- */
void playerUpdate()
{
    if (!mh || !g_state.playing || g_state.paused)
        return;

    // Keep at least ~100ms buffered
    const size_t TARGET_SAMPLES =
        g_state.sampleRate * g_state.channels / 10;

    while (audio.availableRead() < TARGET_SAMPLES) {
        unsigned char buffer[DECODE_BUFFER];
        size_t done = 0;

        int err = mpg123_read(mh, buffer, sizeof(buffer), &done);
        if (done == 0) {
            if (err == MPG123_DONE)
            {
                g_waitForDrain = true;

                if (g_playbackState != STATE_CROSSFADING)
                {
                    g_playbackState = STATE_DRAINING;
                }
            }
            break;
        }


        int frames = done / (sizeof(int16_t) * g_state.channels);
        samplesPlayed += frames;
        g_state.elapsedSeconds =
            (int)(samplesPlayed / g_state.sampleRate);

        static float floatPCM[4096 * 2];
        processSamplesToFloat(
            (int16_t*)buffer,
            floatPCM,
            frames,
            g_state.channels
        );

        if (g_playbackState == STATE_CROSSFADING && mh_next)
        {
            unsigned char buffer2[DECODE_BUFFER];
            size_t done2 = 0;

            mpg123_read(mh_next, buffer2, sizeof(buffer2), &done2);

            if (done2 > 0)
            {
                int frames2 = done2 / (sizeof(int16_t) * g_state.channels);

                static float floatPCM2[4096 * 2];

                processSamplesToFloat(
                    (int16_t*)buffer2,
                    floatPCM2,
                    frames2,
                    g_state.channels
                );

                // Use MIN frames to avoid overflow
                int mixFrames = (frames2 < frames) ? frames2 : frames;

                float duration = g_settings.crossfadeSeconds;
                if (duration < 0.01f) duration = 0.01f;

                float t = g_crossfadeProgress / duration;
                if (!g_metadataSwitched && t >= 0.5f)
                {
                    int nextIndex = (g_crossfadeTargetIndex >= 0)
                      ? g_crossfadeTargetIndex
                      : playerPeekNextIndex();
                    if (nextIndex < 0)
                    {
                        playerStop();
                        return;
                    }

                    if (nextIndex >= 0)
                        playlistSetCurrentIndex(nextIndex);

                    g_metadataSwitched = true;
                }

                if (t > 1.0f) t = 1.0f;

                float fadeOut = cosf(t * 1.5707963f); // cos(t * pi/2)
                float fadeIn  = sinf(t * 1.5707963f);

                for (int i = 0; i < mixFrames * 2; i++)
                {
                    floatPCM[i] *= fadeOut;
                    floatPCM[i] += floatPCM2[i] * fadeIn;
                }

                g_crossfadeProgress +=
                    (float)mixFrames / g_state.sampleRate;

                    if (g_crossfadeProgress >= duration)
                    {
                        mpg123_close(mh);
                        mpg123_delete(mh);

                        mh = mh_next;
                        mh_next = nullptr;

                        g_crossfading = false;
                        g_playbackState = STATE_PLAYING;
                        g_waitForDrain = false;

                        int nextIndex = (g_crossfadeTargetIndex >= 0)
                          ? g_crossfadeTargetIndex
                          : playerPeekNextIndex();
                          playerCommitNextTrack(nextIndex);
                          g_crossfadeTargetIndex = -1;
                        samplesPlayed = (uint64_t)(g_crossfadeProgress * g_state.sampleRate);
                        g_state.elapsedSeconds = (int)g_crossfadeProgress;

                        off_t len = mpg123_length(mh);
                        g_state.durationSeconds =
                            (len > 0) ? (int)(len / g_state.sampleRate) : 0;
                    }
            }
        }

        audio.pushPCM(floatPCM, frames * 2);

        if (err == MPG123_DONE)
        {
            if (!g_crossfading)
            {
                if (g_settings.crossfadeSeconds <= 0.01f)
                {
                    int nextIndex = (g_crossfadeTargetIndex >= 0)
                      ? g_crossfadeTargetIndex
                      : playerPeekNextIndex();
                    if (nextIndex < 0)
                    {
                        playerStop();
                        return;
                    }

                    if (openNextDecoder(nextIndex))
                    {
                        mpg123_close(mh);
                        mpg123_delete(mh);

                        mh = mh_next;
                        mh_next = nullptr;

                        // consume queue/shuffle
                        if (!g_playQueue.empty())
                        {
                            g_playQueue.erase(g_playQueue.begin());
                        }
                        else if (g_state.shuffle)
                        {
                            if (g_state.trackIndex >= 0)
                                g_shuffleHistory.push_back(g_state.trackIndex);

                            if (!g_shufflePool.empty())
                                g_shufflePool.pop_back();
                        }

                        g_state.trackIndex = nextIndex;
                        playlistSetCurrentIndex(nextIndex);

                        samplesPlayed = 0;
                        g_state.elapsedSeconds = 0;

                        return;
                    }
                }

                g_waitForDrain = true;
            }
        }

    }
    int remaining =
        g_state.durationSeconds - g_state.elapsedSeconds;
        if (g_playbackState == STATE_PLAYING &&
        g_settings.crossfadeEnabled &&
        remaining <= (int)g_settings.crossfadeSeconds)
    {
        playerStartCrossfade();
    }
    // Track finished AND buffer drained → next song
    if (g_playbackState == STATE_DRAINING &&
    !g_crossfading &&
    audio.availableRead() == 0)
    {
        g_waitForDrain = false;

        if (g_playbackState == STATE_DRAINING)
            playerNext();
    }
}

/* ---------------------------------------------------- */
/* GETTERS                                              */
/* ---------------------------------------------------- */
const PlayerState* playerGetState() { return &g_state; }
bool playerIsPlaying() { return g_state.playing; }
bool playerIsPaused() { return g_state.playing && g_state.paused; }
int playerGetCurrentTrackIndex() { return g_state.trackIndex; }
int playerGetElapsedSeconds() { return g_state.elapsedSeconds; }
int playerGetTrackLength() { return g_state.durationSeconds; }
