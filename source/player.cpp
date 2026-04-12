#include "player.h"
#include "player_state.h"
#include "playlist.h"
#include "mp3.h"
#include "flac.h"
#include "eq.h"
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
#define FFT_SIZE       1024
#define DECODE_BUFFER  8192   // bytes (mpg123 output)
#define SHUFFLE_MEMORY 5
#define FLOAT_BUF_FRAMES 4096 // frames per decode chunk
/* ---------------------------------------------------- */
/* AUDIO FORMAT                                         */
/* ---------------------------------------------------- */
enum AudioFormat { FORMAT_MP3, FORMAT_FLAC };

static AudioFormat trackFormat(const char* path)
{
    if (!path) return FORMAT_MP3;
    const char* ext = strrchr(path, '.');
    if (ext && strcasecmp(ext, ".flac") == 0) return FORMAT_FLAC;
    return FORMAT_MP3;
}
/* ---------------------------------------------------- */
/* GLOBALS                                              */
/* ---------------------------------------------------- */
static AudioEngine audio;

// Current track decoder
static mpg123_handle* mh      = nullptr;  // active when format == FORMAT_MP3
static FlacDecoder*   mh_flac = nullptr;  // active when format == FORMAT_FLAC
static AudioFormat    g_format = FORMAT_MP3;

// Next track decoder (crossfade / gapless preload)
static mpg123_handle* mh_next      = nullptr;
static FlacDecoder*   mh_flac_next = nullptr;
static AudioFormat    g_formatNext  = FORMAT_MP3;

static std::vector<int> g_playQueue;

/* Crossfade */
static int   g_crossfadeTargetIndex = -1;
static float g_crossfadeProgress    = 0.0f;
static bool  g_metadataSwitched     = false;

/* Time tracking — both in decoded sample frames */
static uint64_t samplesPlayed     = 0; // frames decoded from current track
static uint64_t samplesPlayedNext = 0; // frames decoded from incoming track during xfade

/* Gapless preload guard */
static bool g_preloadAttempted = false;

enum PlaybackState
{
    STATE_STOPPED,
    STATE_PLAYING,
    STATE_CROSSFADING,
    STATE_DRAINING
};
static PlaybackState g_playbackState = STATE_STOPPED;

/* Shuffle */
static void stopPlaybackInternal();
static void rebuildShufflePool();
static std::vector<int> g_shufflePool;
static std::vector<int> g_shuffleHistory;

/* FFT */
float g_fftInput[FFT_SIZE] = {0};
static int fftWritePos = 0;

/* Mixing */
static float g_volume   = 1.0f;
static float g_pan      = 0.0f;
static float g_leftMix  = 1.0f;
static float g_rightMix = 1.0f;

/* ---------------------------------------------------- */
/* FORMAT-AWARE DECODER HELPERS                         */
/* ---------------------------------------------------- */

// Read from whichever decoder is active for the current track.
// Returns same semantics as mpg123_read: MPG123_OK / MPG123_DONE / error.
static int decoderRead(unsigned char* buf, size_t bufBytes, size_t* done)
{
    *done = 0;
    if (g_format == FORMAT_FLAC)
    {
        if (!mh_flac) return -1;
        FlacReadResult r = flacRead(mh_flac, buf, bufBytes, done);
        if (r == FLAC_READ_DONE) return MPG123_DONE;
        if (r == FLAC_READ_ERR)  return -1;
        return MPG123_OK;
    }
    else
    {
        if (!mh) return -1;
        return mpg123_read(mh, buf, bufBytes, done);
    }
}

// Read from the next-track decoder (crossfade / gapless).
static int decoderReadNext(unsigned char* buf, size_t bufBytes, size_t* done)
{
    *done = 0;
    if (g_formatNext == FORMAT_FLAC)
    {
        if (!mh_flac_next) return -1;
        FlacReadResult r = flacRead(mh_flac_next, buf, bufBytes, done);
        if (r == FLAC_READ_DONE) return MPG123_DONE;
        if (r == FLAC_READ_ERR)  return -1;
        return MPG123_OK;
    }
    else
    {
        if (!mh_next) return -1;
        return mpg123_read(mh_next, buf, bufBytes, done);
    }
}

// Close and null-out the current decoder.
static void closeCurrentDecoder()
{
    if (mh)
    {
        mpg123_close(mh);
        mpg123_delete(mh);
        mh = nullptr;
    }
    if (mh_flac)
    {
        flacClose(mh_flac);
        mh_flac = nullptr;
    }
}

// Close and null-out the next-track decoder.
static void closeNextDecoderAll()
{
    if (mh_next)
    {
        mpg123_close(mh_next);
        mpg123_delete(mh_next);
        mh_next = nullptr;
    }
    if (mh_flac_next)
    {
        flacClose(mh_flac_next);
        mh_flac_next = nullptr;
    }
}

// Promote next → current (swap pointers, no decoding).
static void promoteNextToCurrent()
{
    closeCurrentDecoder();
    mh        = mh_next;       mh_next      = nullptr;
    mh_flac   = mh_flac_next;  mh_flac_next = nullptr;
    g_format  = g_formatNext;
}

// Get format info from whichever decoder is current.
static bool decoderGetFormat(long* rate, int* ch)
{
    if (g_format == FORMAT_FLAC && mh_flac)
    {
        *rate = (long)mh_flac->sampleRate;
        *ch   = (int)mh_flac->channels;
        return (*rate > 0);
    }
    else if (mh)
    {
        int enc;
        return mpg123_getformat(mh, rate, ch, &enc) == MPG123_OK;
    }
    return false;
}

// Get format info from the next decoder.
static bool decoderGetFormatNext(long* rate, int* ch)
{
    if (g_formatNext == FORMAT_FLAC && mh_flac_next)
    {
        *rate = (long)mh_flac_next->sampleRate;
        *ch   = (int)mh_flac_next->channels;
        return (*rate > 0);
    }
    else if (mh_next)
    {
        int enc;
        return mpg123_getformat(mh_next, rate, ch, &enc) == MPG123_OK;
    }
    return false;
}

// Total sample frames in the current stream (-1 if unknown).
static int64_t decoderTotalSamples()
{
    if (g_format == FORMAT_FLAC && mh_flac)
        return (int64_t)mh_flac->totalSamples;
    else if (mh)
    {
        off_t len = mpg123_length(mh);
        return (len > 0) ? (int64_t)len : -1;
    }
    return -1;
}

// Is any current decoder open?
static bool decoderIsOpen()
{
    return (g_format == FORMAT_FLAC) ? (mh_flac != nullptr) : (mh != nullptr);
}


PlayerState g_state = {
    .trackIndex      = -1,
    .elapsedSeconds  = 0,
    .durationSeconds = 0,
    .sampleRate      = 0,
    .channels        = 0,
    .playing         = false,
    .paused          = false,
    .shuffle         = false,
    .repeat          = REPEAT_OFF
};

/* ---------------------------------------------------- */
/* INTERNAL HELPERS                                     */
/* ---------------------------------------------------- */

// Safely destroy mh_next / mh_flac_next and reset to nullptr.
static void closeNextDecoder()
{
    closeNextDecoderAll();
}

// Open and configure the decoder for track at `index` into the next-track slot.
static bool openNextDecoder(int index)
{
    closeNextDecoderAll(); // ensure no stale handle

    const char* path = playlistGetTrack(index);
    if (!path) return false;

    g_formatNext = trackFormat(path);

    if (g_formatNext == FORMAT_FLAC)
    {
        mh_flac_next = flacOpen(path);
        return (mh_flac_next != nullptr);
    }
    else
    {
        mh_next = mpg123_new(nullptr, nullptr);
        if (!mh_next) return false;

        mpg123_param(mh_next, MPG123_GAPLESS,      1,                 0);
        mpg123_param(mh_next, MPG123_ADD_FLAGS,    MPG123_SKIP_ID3V2, 0);
        mpg123_param(mh_next, MPG123_FORCE_STEREO, 1,                 0);

        if (mpg123_open(mh_next, path) != MPG123_OK)
        { closeNextDecoderAll(); return false; }

        long rate; int ch, enc;
        if (mpg123_getformat(mh_next, &rate, &ch, &enc) != MPG123_OK)
        { closeNextDecoderAll(); return false; }

        mpg123_format_none(mh_next);
        mpg123_format(mh_next, rate, ch, MPG123_ENC_SIGNED_16);
        return true;
    }
}

// Commit the incoming track as the new current track.
// Only updates bookkeeping — does NOT stop/start playback.
// BUG FIX: removed the playerStop() call that was here when nextIndex < 0.
// Calling playerStop() from inside the decode loop destroyed mh/mh_next while
// we were still using them. Callers must check nextIndex before calling this.
static void playerCommitNextTrack(int nextIndex)
{
    // Pop from queue if this track came from it
    if (!g_playQueue.empty() && g_playQueue.front() == nextIndex)
    {
        g_playQueue.erase(g_playQueue.begin());
    }
    else if (g_state.shuffle && !g_shufflePool.empty())
    {
        g_shuffleHistory.push_back(g_state.trackIndex);
        if (g_shuffleHistory.size() > SHUFFLE_MEMORY)
            g_shuffleHistory.erase(g_shuffleHistory.begin());
        // Remove the chosen track from the pool
        auto it = std::find(g_shufflePool.begin(), g_shufflePool.end(), nextIndex);
        if (it != g_shufflePool.end())
            g_shufflePool.erase(it);
    }

    g_state.trackIndex = nextIndex;
    playlistSetCurrentIndex(nextIndex);
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

    /* QUEUE */
    if (!g_playQueue.empty())
        return g_playQueue.front();

    /* REPEAT ONE */
    if (g_state.repeat == REPEAT_ONE && g_state.trackIndex >= 0)
        return g_state.trackIndex;

    /* SHUFFLE */
    if (g_state.shuffle)
    {
        if (g_shufflePool.empty())
        {
            if (g_state.repeat == REPEAT_ALL)
            {
                rebuildShufflePool();
                // Avoid immediately re-playing the same track
                auto it = std::find(g_shufflePool.begin(), g_shufflePool.end(), g_state.trackIndex);
                if (it != g_shufflePool.end())
                    g_shufflePool.erase(it);
            }
            else
            {
                return -1; // end of shuffle, no repeat
            }
        }
        return g_shufflePool.empty() ? -1 : g_shufflePool.back();
    }

    /* NORMAL */
    int next = g_state.trackIndex + 1;
    if (next >= count)
        return (g_state.repeat == REPEAT_ALL) ? 0 : -1;

    return next;
}

/* ---------------------------------------------------- */
/* DSP                                                  */
/* ---------------------------------------------------- */
static void processSamplesToFloat(
    const int16_t* in,
    float*         out,
    int            frames,
    int            channels
) {
    for (int i = 0; i < frames; i++)
    {
        float left  = in[i * channels]     / 32768.0f;
        float right = (channels > 1) ? in[i * channels + 1] / 32768.0f : left;
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
void playerApplyVolumePan()
{
    float left  = g_volume;
    float right = g_volume;
    if      (g_pan < 0.0f) right *= (1.0f + g_pan);
    else if (g_pan > 0.0f) left  *= (1.0f - g_pan);
    g_leftMix  = (left  < 0.0f) ? 0.0f : left;
    g_rightMix = (right < 0.0f) ? 0.0f : right;
}

void playerAdjustVolume(float delta) { playerSetVolume(g_volume + delta); }

void playerSetVolume(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    g_volume = v;
    playerApplyVolumePan();
}

float playerGetVolume() { return g_volume; }

void playerSetPan(float pan)
{
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;
    g_pan = pan;
    playerApplyVolumePan();
}

float playerGetPan() { return g_pan; }

/* ---------------------------------------------------- */
/* REPLAY GAIN                                          */
/* ---------------------------------------------------- */
void applyReplayGainFromMetadata(const Mp3MetadataEntry& meta)
{
    float db = 0.0f;
    switch (g_settings.replayGainMode)
    {
        case REPLAYGAIN_TRACK:
            if (meta.hasTrackReplayGain) db = meta.replayGainDb;
            break;
        case REPLAYGAIN_ALBUM:
            if (meta.hasAlbumReplayGain) db = meta.replayGainAlbumDb;
            break;
        case REPLAYGAIN_OFF:
        default:
            db = 0.0f;
            break;
    }
    g_equalizer.setReplayGainDb(db);
}

/* ---------------------------------------------------- */
/* LIFECYCLE                                            */
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
/* INTERNAL STOP                                        */
/* ---------------------------------------------------- */
static void stopPlaybackInternal()
{
    audio.stop();

    g_playbackState      = STATE_STOPPED;
    g_crossfadeTargetIndex = -1;
    g_metadataSwitched   = false;
    g_crossfadeProgress  = 0.0f;
    samplesPlayedNext    = 0;
    g_preloadAttempted   = false;

    closeNextDecoderAll();
    closeCurrentDecoder();

    audio.shutdown();
    samplesPlayed = 0;
}

/* ---------------------------------------------------- */
/* PLAYBACK CONTROL                                     */
/* ---------------------------------------------------- */
void playerPlay(int index)
{
    stopPlaybackInternal();

    const char* path = playlistGetTrack(index);
    if (!path)
    {
        printf("Error: invalid track path\n");
        return;
    }

    g_format = trackFormat(path);

    long rate = 0;
    int  ch   = 0;

    if (g_format == FORMAT_FLAC)
    {
        mh_flac = flacOpen(path);
        if (!mh_flac)
        {
            printf("Error: flacOpen failed for %s\n", path);
            return;
        }
        rate = (long)mh_flac->sampleRate;
        ch   = (int)mh_flac->channels;
    }
    else
    {
        mh = mpg123_new(nullptr, nullptr);
        if (!mh) { printf("Error: mpg123_new failed\n"); return; }

        mpg123_param(mh, MPG123_GAPLESS,      1,                 0);
        mpg123_param(mh, MPG123_ADD_FLAGS,    MPG123_SKIP_ID3V2, 0);
        mpg123_param(mh, MPG123_FORCE_STEREO, 1,                 0);

        if (mpg123_open(mh, path) != MPG123_OK)
        {
            printf("Error: failed to open %s\n", path);
            mpg123_delete(mh); mh = nullptr;
            return;
        }

        int enc;
        if (mpg123_getformat(mh, &rate, &ch, &enc) != MPG123_OK)
        {
            printf("Error: failed to get format\n");
            mpg123_close(mh); mpg123_delete(mh); mh = nullptr;
            return;
        }

        mpg123_format_none(mh);
        mpg123_format(mh, rate, ch, MPG123_ENC_SIGNED_16);
    }

    g_state.sampleRate = rate;
    g_state.channels   = ch;

    audio.init(rate, ch);
    audio.start();
    audio.setPaused(false);

    // Duration: for FLAC use exact sample count; for MP3 use mpg123_length
    int64_t totalSamples = decoderTotalSamples();
    g_state.durationSeconds =
        (totalSamples > 0 && rate > 0) ? (int)(totalSamples / rate) : 0;

    samplesPlayed          = 0;
    g_state.elapsedSeconds = 0;
    g_preloadAttempted     = false;

    // ReplayGain — try FLAC metadata first, fall back to MP3 metadata
    if (g_format == FORMAT_FLAC)
    {
        const Mp3MetadataEntry* meta = flacGetTrackMetadata(index);
        if (meta) applyReplayGainFromMetadata(*meta);
    }
    else
    {
        const Mp3MetadataEntry* meta = mp3GetTrackMetadata(index);
        if (meta) applyReplayGainFromMetadata(*meta);
    }

    g_state.trackIndex = index;
    g_state.playing    = true;
    g_state.paused     = false;

    playlistSetCurrentIndex(index);
    g_playbackState = STATE_PLAYING;

    printf("Playing [%s]: %s\n",
           (g_format == FORMAT_FLAC) ? "FLAC" : "MP3", path);
}

void playerStop()
{
    stopPlaybackInternal();
    spectrumReset();
    g_shuffleHistory.clear();
    g_shufflePool.clear();
    g_state.playing          = false;
    g_state.paused           = false;
    g_state.trackIndex       = -1;
    g_state.elapsedSeconds   = 0;
    g_state.durationSeconds  = 0;
    g_playbackState          = STATE_STOPPED;
}

void playerTogglePause()
{
    if (!g_state.playing)
        return;
    g_state.paused = !g_state.paused;
    audio.setPaused(g_state.paused);
}

// BUG FIX: playerNext() previously manually popped shuffle/queue AND then
// called playerPlay() → playerCommitNextTrack() which popped them a second
// time. Now playerNext() just calls playerPlay(); commit happens once inside
// the gapless/crossfade switch paths or via playerCommitNextTrack() below.
void playerNext()
{
    int nextIndex = playerPeekNextIndex();
    if (nextIndex < 0)
    {
        playerStop();
        return;
    }
    printf("NEXT → %d\n", nextIndex);
    playerPlay(nextIndex);
}

void playerPrev()
{
    if (g_state.shuffle && !g_shuffleHistory.empty())
    {
        int prevIndex = g_shuffleHistory.back();
        g_shuffleHistory.pop_back();
        playerPlay(prevIndex);
        return;
    }

    int count = playlistGetCount();
    if (count == 0)
        return;

    int prevIndex = g_state.trackIndex - 1;
    if (prevIndex < 0)
        prevIndex = (g_state.repeat == REPEAT_ALL) ? count - 1 : 0;

    playerPlay(prevIndex);
}

void playerEnqueue(int index)
{
    if (index >= 0 && index < playlistGetCount())
        g_playQueue.push_back(index);
}

// Manual crossfade trigger (e.g. from controller button)
void playerStartCrossfade()
{
    if (!g_settings.crossfadeEnabled)
        return;
    if (g_playbackState == STATE_CROSSFADING || mh_next != nullptr || mh_flac_next != nullptr)
        return;

    int nextIndex = playerPeekNextIndex();
    if (nextIndex < 0)
    {
        playerStop();
        return;
    }

    if (!openNextDecoder(nextIndex))
    {
        playerNext();
        return;
    }

    g_crossfadeTargetIndex = nextIndex;
    samplesPlayedNext      = 0;
    g_playbackState        = STATE_CROSSFADING;
    g_crossfadeProgress    = 0.0f;
    g_metadataSwitched     = false;
}

void playerSeek(float targetSeconds)
{
    if (!decoderIsOpen() || !g_state.playing)
        return;

    if (targetSeconds < 0.0f)
        targetSeconds = 0.0f;
    if (targetSeconds > (float)g_state.durationSeconds)
        targetSeconds = (float)g_state.durationSeconds;

    uint64_t targetSample = (uint64_t)(targetSeconds * g_state.sampleRate);

    audio.setPaused(true);

    bool ok = false;
    if (g_format == FORMAT_FLAC && mh_flac)
        ok = flacSeek(mh_flac, targetSample);
    else if (mh)
        ok = (mpg123_seek(mh, (off_t)targetSample, SEEK_SET) >= 0);

    if (ok)
    {
        samplesPlayed          = targetSample;
        g_state.elapsedSeconds = (int)targetSeconds;
    }

    audio.setPaused(g_state.paused);

    // Cancel any pending crossfade/preload — recalculated on next update
    g_crossfadeTargetIndex = -1;
    g_metadataSwitched     = false;
    g_preloadAttempted     = false;
    closeNextDecoderAll();
}

/* ---------------------------------------------------- */
/* SHUFFLE / REPEAT                                     */
/* ---------------------------------------------------- */
bool playerIsShuffleEnabled() { return g_state.shuffle; }

void playerToggleShuffle()
{
    g_state.shuffle = !g_state.shuffle;
    if (g_state.shuffle)
    {
        rebuildShufflePool();
        g_shuffleHistory.clear();
        // Don't include the currently playing track in the upcoming pool
        auto it = std::find(g_shufflePool.begin(), g_shufflePool.end(), g_state.trackIndex);
        if (it != g_shufflePool.end())
            g_shufflePool.erase(it);
    }
    else
    {
        g_shufflePool.clear();
        g_shuffleHistory.clear();
    }
}

RepeatMode playerGetRepeatMode()    { return g_state.repeat; }
bool       playerIsRepeatEnabled()  { return g_state.repeat != REPEAT_OFF; }

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
    return g_state.playing ? (float)g_state.elapsedSeconds : 0.0f;
}

/* ---------------------------------------------------- */
/* UPDATE LOOP                                          */
/* ---------------------------------------------------- */
void playerUpdate()
{
    if (!decoderIsOpen() || !g_state.playing || g_state.paused)
        return;

    const size_t TARGET_SAMPLES = g_state.sampleRate * g_state.channels / 10;

    // Stack buffers — avoids the static aliasing hazard of the original
    float floatPCM[FLOAT_BUF_FRAMES * 2];
    float pcm1    [FLOAT_BUF_FRAMES * 2];
    float pcm2    [FLOAT_BUF_FRAMES * 2];

    while (audio.availableRead() < TARGET_SAMPLES)
    {
        unsigned char buffer[DECODE_BUFFER];
        size_t done = 0;
        int err = decoderRead(buffer, sizeof(buffer), &done);

        /* ================================================= */
        /* STATE: PLAYING                                     */
        /* ================================================= */
        if (g_playbackState == STATE_PLAYING)
        {
            if (done > 0)
            {
                int frames = (int)(done / (sizeof(int16_t) * g_state.channels));
                samplesPlayed          += frames;
                g_state.elapsedSeconds  = (int)(samplesPlayed / g_state.sampleRate);

                processSamplesToFloat((int16_t*)buffer, floatPCM, frames, g_state.channels);
                audio.pushPCM(floatPCM, frames * 2);
            }

            // Compute AFTER updating samplesPlayed.
            // int64_t avoids unsigned underflow when samplesPlayed slightly
            // overshoots the duration estimate (common with VBR files).
            int64_t samplesRemaining =
                ((int64_t)g_state.durationSeconds * g_state.sampleRate)
                - (int64_t)samplesPlayed;

            /* ---- gapless preload (crossfade OFF) ---- */
            if (!g_settings.crossfadeEnabled &&
                mh_next == nullptr && mh_flac_next == nullptr &&
                !g_preloadAttempted &&
                samplesRemaining <= (int64_t)g_state.sampleRate) // 1 s window
            {
                int nextIndex = playerPeekNextIndex();
                if (nextIndex >= 0 && openNextDecoder(nextIndex))
                    g_preloadAttempted = true;
            }

            /* ---- stream ended: gapless hard-switch ---- */
            if (err == MPG123_DONE)
            {
                int nextIndex = playerPeekNextIndex();
                if (nextIndex >= 0)
                {
                    // Use preloaded decoder if ready, otherwise open now
                    if (mh_next == nullptr && mh_flac_next == nullptr)
                        openNextDecoder(nextIndex);

                    if (mh_next != nullptr || mh_flac_next != nullptr)
                    {
                        promoteNextToCurrent();
                        playerCommitNextTrack(nextIndex);

                        long rate; int ch;
                        if (decoderGetFormat(&rate, &ch))
                        {
                            g_state.sampleRate = rate;
                            g_state.channels   = ch;
                        }

                        samplesPlayed          = 0;
                        g_state.elapsedSeconds = 0;
                        g_preloadAttempted     = false;

                        int64_t total = decoderTotalSamples();
                        g_state.durationSeconds =
                            (total > 0 && g_state.sampleRate > 0)
                            ? (int)(total / g_state.sampleRate) : 0;

                        continue; // decode next track immediately, no gap
                    }
                }

                // No next track (or open failed) — stop cleanly
                closeNextDecoder();
                g_playbackState = STATE_DRAINING;
                break;
            }

            /* ---- crossfade trigger ---- */
            int64_t crossfadeSamples =
                (int64_t)(g_settings.crossfadeSeconds * g_state.sampleRate);

            if (g_settings.crossfadeEnabled &&
                mh_next == nullptr && mh_flac_next == nullptr &&
                samplesRemaining > 0 &&
                samplesRemaining <= crossfadeSamples)
            {
                int nextIndex = playerPeekNextIndex();
                if (nextIndex >= 0 && openNextDecoder(nextIndex))
                {
                    g_crossfadeTargetIndex = nextIndex;
                    g_crossfadeProgress    = 0.0f;
                    g_metadataSwitched     = false;
                    samplesPlayedNext      = 0;
                    g_playbackState        = STATE_CROSSFADING;
                }
                else if (nextIndex < 0)
                {
                    // Last track, crossfade disabled for this transition
                    // (no next track to fade into — just let it play out)
                }
            }
        }
        /* ================================================= */
        /* STATE: CROSSFADING                                 */
        /* ================================================= */
        else if (g_playbackState == STATE_CROSSFADING)
        {
            unsigned char buffer2[DECODE_BUFFER];
            size_t done2 = 0;
            decoderReadNext(buffer2, sizeof(buffer2), &done2);

            if (done == 0 && done2 == 0)
            {
                // Both streams exhausted — drain whatever's left in the hw buffer
                g_playbackState = STATE_DRAINING;
                break;
            }

            int frames1   = (done  > 0) ? (int)(done  / (sizeof(int16_t) * g_state.channels)) : 0;
            int frames2   = (done2 > 0) ? (int)(done2 / (sizeof(int16_t) * g_state.channels)) : 0;
            int mixFrames = std::min(frames1, frames2);

            float duration = g_settings.crossfadeSeconds;
            if (duration < 0.01f) duration = 0.01f;

            float t = g_crossfadeProgress / duration;
            if (t > 1.0f) t = 1.0f;

            float fadeOut = cosf(t * 1.5707963f); // cos(0..90°): 1 → 0
            float fadeIn  = sinf(t * 1.5707963f); // sin(0..90°): 0 → 1

            if (mixFrames > 0)
            {
                processSamplesToFloat((int16_t*)buffer,  pcm1, mixFrames, g_state.channels);
                processSamplesToFloat((int16_t*)buffer2, pcm2, mixFrames, g_state.channels);

                for (int i = 0; i < mixFrames * 2; i++)
                    pcm1[i] = pcm1[i] * fadeOut + pcm2[i] * fadeIn;

                audio.pushPCM(pcm1, mixFrames * 2);

                samplesPlayedNext   += frames2;
                g_crossfadeProgress += (float)frames2 / g_state.sampleRate;
            }
            else
            {
                // One stream is dry but the other has data — advance time so we don't stall
                int liveFrames = std::max(frames1, frames2);
                g_crossfadeProgress += (float)liveFrames / g_state.sampleRate;
                samplesPlayedNext   += (uint64_t)frames2;
            }

            /* ---- switch metadata at the halfway point ---- */
            if (!g_metadataSwitched && t >= 0.5f)
            {
                playerCommitNextTrack(g_crossfadeTargetIndex);
                g_metadataSwitched = true;
            }

            /* ---- crossfade complete ---- */
            if (g_crossfadeProgress >= duration)
            {
                // Commit metadata now if we somehow never hit the 0.5 threshold
                if (!g_metadataSwitched)
                {
                    playerCommitNextTrack(g_crossfadeTargetIndex);
                    g_metadataSwitched = true;
                }

                promoteNextToCurrent();

                long rate; int ch;
                if (decoderGetFormat(&rate, &ch))
                {
                    g_state.sampleRate = rate;
                    g_state.channels   = ch;
                }

                // samplesPlayedNext is how far into the new track we already are —
                // set samplesPlayed to it so elapsed time is correct and the crossfade
                // trigger doesn't immediately re-fire on the very next update.
                samplesPlayed          = samplesPlayedNext;
                g_state.elapsedSeconds = (int)(samplesPlayed / g_state.sampleRate);

                int64_t total = decoderTotalSamples();
                g_state.durationSeconds =
                    (total > 0 && g_state.sampleRate > 0)
                    ? (int)(total / g_state.sampleRate) : 0;

                g_crossfadeTargetIndex = -1;
                g_metadataSwitched     = false;
                g_preloadAttempted     = false;
                g_playbackState        = STATE_PLAYING;
            }
        }
        /* ================================================= */
        /* STATE: DRAINING                                    */
        /* ================================================= */
        else if (g_playbackState == STATE_DRAINING)
        {
            break; // just wait for the hw buffer to empty
        }
    } // end while

    /* ---- drain complete → decide what to do next ---- */
    if (g_playbackState == STATE_DRAINING && audio.availableRead() == 0)
    {
        int nextIndex = playerPeekNextIndex();
        if (nextIndex >= 0)
            playerNext();      // more tracks to play
        else
            playerStop();      // BUG FIX: was calling playerNext() which called
                               // playerStop() anyway, but left a frame where
                               // g_state.playing was true with a dead decoder.
    }
}

/* ---------------------------------------------------- */
/* GETTERS                                              */
/* ---------------------------------------------------- */
const PlayerState* playerGetState()          { return &g_state; }
bool               playerIsPlaying()         { return g_state.playing; }
bool               playerIsPaused()          { return g_state.playing && g_state.paused; }
int                playerGetCurrentTrackIndex() { return g_state.trackIndex; }
int                playerGetElapsedSeconds() { return g_state.elapsedSeconds; }
int                playerGetTrackLength()    { return g_state.durationSeconds; }
