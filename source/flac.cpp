#include "flac.h"
#include "playlist.h"
#include "player.h"
#include <FLAC/stream_decoder.h>
#include <FLAC/metadata.h>
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

/* -------------------------------------------------------
   Cache
------------------------------------------------------- */
#define FLAC_CACHE_VERSION 1
#define FLAC_CACHE_PATH     "sdmc:/config/winamp/flac_cache.bin"
#define FLAC_CACHE_TMP_PATH "sdmc:/config/winamp/flac_cache.bin.tmp"

struct FlacCacheHeader {
    uint32_t magic;    // 'FLCC'
    uint32_t version;
};

struct FlacCacheEntry {
    char             path[512];
    time_t           mtime;
    Mp3MetadataEntry meta;
};

static std::unordered_map<std::string, FlacCacheEntry> g_flacCache;
static bool g_flacCacheLoaded = false;

/* -------------------------------------------------------
   Background scanner state
------------------------------------------------------- */
static Thread g_flacThread;
static bool   g_flacThreadRunning = false;
static Mutex  g_flacMetaMutex;
static Mutex  g_flacScanMutex;
static char   g_flacLoadedFolder[512] = {0};

struct FlacScanJob {
    std::string path;
    int         index;
    int         generation;
};

static std::vector<FlacScanJob>            g_flacScanQueue;
static std::unordered_set<std::string>     g_flacScanQueued;
static std::vector<RuntimeMetadata>        g_flacPlaylistMeta;
static int                                 g_flacScanGeneration = 0;

/* -------------------------------------------------------
   Helpers
------------------------------------------------------- */
static bool flacCacheValid(const FlacCacheEntry& e)
{
    struct stat st;
    if (stat(e.path, &st) != 0) return false;
    return st.st_mtime == e.mtime;
}

static bool flacPlaylistHasPath(const char* path)
{
    for (auto& m : g_flacPlaylistMeta)
        if (strcmp(m.path, path) == 0) return true;
    return false;
}

static void ensureCacheDir()
{
    mkdir("sdmc:/config",        0777);
    mkdir("sdmc:/config/winamp", 0777);
}

/* -------------------------------------------------------
   Cache I/O
------------------------------------------------------- */
void flacLoadCache(const char* /*folderKey*/)
{
    if (g_flacCacheLoaded) return;
    ensureCacheDir();

    FILE* f = fopen(FLAC_CACHE_PATH, "rb");
    if (!f) { g_flacCacheLoaded = true; return; }

    FlacCacheHeader h{};
    if (fread(&h, sizeof(h), 1, f) != 1 ||
        h.magic   != 0x464C4343 ||          // 'FLCC'
        h.version != FLAC_CACHE_VERSION)
    {
        fclose(f);
        g_flacCacheLoaded = true;
        return;
    }

    FlacCacheEntry e;
    while (fread(&e, sizeof(e), 1, f) == 1)
        if (flacCacheValid(e))
            g_flacCache[e.path] = e;

    fclose(f);
    g_flacCacheLoaded = true;
}

static void flacAppendCache(const char* path, const Mp3MetadataEntry& meta)
{
    if (!path || strncmp(path, "romfs:/", 7) == 0) return;
    if (strstr(path, ".tmp")) return;

    struct stat st;
    if (stat(path, &st) != 0) return;

    // Write throttle — SD cards hate hammering
    static time_t lastWrite = 0;
    time_t now = time(nullptr);
    if (now - lastWrite < 1) return;
    lastWrite = now;

    FlacCacheEntry& e = g_flacCache[path];
    strncpy(e.path, path, sizeof(e.path) - 1);
    e.path[sizeof(e.path)-1] = '\0';
    e.meta  = meta;
    e.mtime = st.st_mtime;

    FILE* f = fopen(FLAC_CACHE_TMP_PATH, "wb");
    if (!f) return;

    FlacCacheHeader h{ 0x464C4343, FLAC_CACHE_VERSION };
    if (fwrite(&h, sizeof(h), 1, f) != 1) goto fail;

    for (auto& [_, entry] : g_flacCache)
        if (fwrite(&entry, sizeof(entry), 1, f) != 1) goto fail;

    fflush(f);
    fsync(fileno(f));
    fclose(f);
    remove(FLAC_CACHE_PATH);
    rename(FLAC_CACHE_TMP_PATH, FLAC_CACHE_PATH);
    return;

fail:
    fclose(f);
    remove(FLAC_CACHE_TMP_PATH);
}

/* -------------------------------------------------------
   Vorbis comment (FLAC tag) parsing
   FLAC stores tags as UTF-8 KEY=VALUE Vorbis comments.
------------------------------------------------------- */
static void parseVorbisComment(const char* key, const char* value,
                                Mp3MetadataEntry& entry)
{
    if (!key || !value) return;

    if (strcasecmp(key, "TITLE") == 0)
    {
        strncpy(entry.title, value, sizeof(entry.title) - 1);
        entry.title[sizeof(entry.title)-1] = '\0';
    }
    else if (strcasecmp(key, "ARTIST") == 0 && entry.artist[0] == '\0')
    {
        strncpy(entry.artist, value, sizeof(entry.artist) - 1);
        entry.artist[sizeof(entry.artist)-1] = '\0';
    }
    else if (strcasecmp(key, "ALBUMARTIST") == 0 && entry.artist[0] == '\0')
    {
        strncpy(entry.artist, value, sizeof(entry.artist) - 1);
        entry.artist[sizeof(entry.artist)-1] = '\0';
    }
    else if (strcasecmp(key, "REPLAYGAIN_TRACK_GAIN") == 0)
    {
        float db = 0.0f;
        if (sscanf(value, "%f", &db) == 1)
        {
            entry.replayGainDb       = db;
            entry.hasTrackReplayGain = true;
        }
    }
    else if (strcasecmp(key, "REPLAYGAIN_ALBUM_GAIN") == 0)
    {
        float db = 0.0f;
        if (sscanf(value, "%f", &db) == 1)
        {
            entry.replayGainAlbumDb  = db;
            entry.hasAlbumReplayGain = true;
        }
    }
}

/* -------------------------------------------------------
   Metadata scan  (runs on background thread)
   Uses libFLAC's metadata iterator — reads tags and
   stream info without decoding any audio frames.
------------------------------------------------------- */
static void readFlacMetadata(const char* path, Mp3MetadataEntry& entry)
{
    memset(&entry, 0, sizeof(entry));
    entry.replayGainPeak      = 1.0f;
    entry.replayGainAlbumPeak = 1.0f;

    FLAC__Metadata_Chain* chain = FLAC__metadata_chain_new();
    if (!chain) return;

    if (!FLAC__metadata_chain_read(chain, path))
    {
        FLAC__metadata_chain_delete(chain);
        return;
    }

    FLAC__Metadata_Iterator* it = FLAC__metadata_iterator_new();
    if (!it) { FLAC__metadata_chain_delete(chain); return; }

    FLAC__metadata_iterator_init(it, chain);

    do {
        FLAC__StreamMetadata* block = FLAC__metadata_iterator_get_block(it);
        if (!block) continue;

        if (block->type == FLAC__METADATA_TYPE_STREAMINFO)
        {
            const FLAC__StreamMetadata_StreamInfo& si = block->data.stream_info;
            entry.sampleRateKHz   = (int)(si.sample_rate / 1000);
            entry.channels        = (int)si.channels;
            entry.bitrateKbps     = 0; // filled below from file size
            if (si.sample_rate > 0 && si.total_samples > 0)
                entry.durationSeconds = (int)(si.total_samples / si.sample_rate);
        }
        else if (block->type == FLAC__METADATA_TYPE_VORBIS_COMMENT)
        {
            const FLAC__StreamMetadata_VorbisComment& vc =
                block->data.vorbis_comment;

            for (uint32_t i = 0; i < vc.num_comments; i++)
            {
                // Each comment is "KEY=VALUE" in UTF-8
                const char* raw = (const char*)vc.comments[i].entry;
                if (!raw) continue;

                // Split on first '='
                const char* eq = strchr(raw, '=');
                if (!eq) continue;

                // Key into a temp buffer
                char key[64] = {0};
                size_t keyLen = (size_t)(eq - raw);
                if (keyLen >= sizeof(key)) keyLen = sizeof(key) - 1;
                memcpy(key, raw, keyLen);
                key[keyLen] = '\0';

                parseVorbisComment(key, eq + 1, entry);
            }
        }

    } while (FLAC__metadata_iterator_next(it));

    FLAC__metadata_iterator_delete(it);
    FLAC__metadata_chain_delete(chain);

    // If title is still blank use filename (without extension)
    if (entry.title[0] == '\0')
    {
        const char* slash = strrchr(path, '/');
        const char* name  = slash ? slash + 1 : path;
        const char* dot   = strrchr(name, '.');
        size_t len = dot ? (size_t)(dot - name) : strlen(name);
        if (len >= sizeof(entry.title)) len = sizeof(entry.title) - 1;
        memcpy(entry.title, name, len);
        entry.title[len] = '\0';
    }

    // Estimate bitrate from file size (lossless, so it varies)
    if (entry.durationSeconds > 0)
    {
        struct stat st;
        if (stat(path, &st) == 0)
            entry.bitrateKbps = (int)((st.st_size * 8LL) /
                                      (entry.durationSeconds * 1000LL));
    }
}

/* -------------------------------------------------------
   Background scanner thread
------------------------------------------------------- */
static void flacScanThread(void*)
{
    while (g_flacThreadRunning)
    {
        FlacScanJob job;

        mutexLock(&g_flacScanMutex);
        if (g_flacScanQueue.empty())
        {
            mutexUnlock(&g_flacScanMutex);
            svcSleepThread(50'000'000); // 50 ms
            continue;
        }
        job = g_flacScanQueue.front();
        g_flacScanQueue.erase(g_flacScanQueue.begin());
        mutexUnlock(&g_flacScanMutex);

        // Cancel check
        mutexLock(&g_flacScanMutex);
        bool canceled = (job.generation != g_flacScanGeneration);
        mutexUnlock(&g_flacScanMutex);
        if (canceled) continue;

        // Don't scan the track that's currently playing
        // Compare by path since job.index is a LOCAL metadata index,
        // not a global playlist index.
        if (playerIsPlaying())
        {
            int globalIdx = playlistGetCurrentIndex();
            const char* playingPath = playlistGetTrack(globalIdx);
            if (playingPath && job.path == playingPath)
                continue;
        }

        Mp3MetadataEntry entry{};
        readFlacMetadata(job.path.c_str(), entry);

        mutexLock(&g_flacMetaMutex);
        if (job.index < (int)g_flacPlaylistMeta.size())
            g_flacPlaylistMeta[job.index].meta = entry;
        mutexUnlock(&g_flacMetaMutex);

        flacAppendCache(job.path.c_str(), entry);
    }
}

/* -------------------------------------------------------
   Public metadata / playlist API
------------------------------------------------------- */
bool flacIsFolderLoaded(const char* path)
{
    return path && strcmp(g_flacLoadedFolder, path) == 0;
}

void flacSetLoadedFolder(const char* path)
{
    if (!path) return;
    strncpy(g_flacLoadedFolder, path, sizeof(g_flacLoadedFolder) - 1);
    g_flacLoadedFolder[sizeof(g_flacLoadedFolder)-1] = '\0';
}

static bool g_flacMutexInited = false;

static void flacEnsureMutexInited()
{
    if (!g_flacMutexInited)
    {
        mutexInit(&g_flacMetaMutex);
        mutexInit(&g_flacScanMutex);
        g_flacMutexInited = true;
    }
}

void flacCancelAllScans()
{
    flacEnsureMutexInited();
    mutexLock(&g_flacScanMutex);
    g_flacScanQueue.clear();
    g_flacScanQueued.clear();
    g_flacScanGeneration++;
    mutexUnlock(&g_flacScanMutex);
}

void flacStartBackgroundScanner()
{
    flacEnsureMutexInited();
    g_flacThreadRunning = true;
    threadCreate(&g_flacThread,
                 flacScanThread,
                 nullptr, nullptr,
                 0x4000, 0x2B, -2);
    threadStart(&g_flacThread);
}

void flacStopBackgroundScanner()
{
    if (!g_flacThreadRunning) return;
    g_flacThreadRunning = false;
    threadWaitForExit(&g_flacThread);
    threadClose(&g_flacThread);
}

void flacClearMetadata()
{
    flacEnsureMutexInited();
    mutexLock(&g_flacMetaMutex);
    g_flacPlaylistMeta.clear();
    mutexUnlock(&g_flacMetaMutex);
}

bool flacAddToPlaylist(const char* path)
{
    if (!path) return false;
    flacEnsureMutexInited();
    if (flacPlaylistHasPath(path)) return false;

    playlistAdd(path);
    // NOTE: We use the LOCAL index into g_flacPlaylistMeta, not the global
    // playlist index. This is critical for mixed MP3+FLAC folders where the
    // global playlist index != the FLAC metadata vector index.
    int localIndex = (int)g_flacPlaylistMeta.size();

    Mp3MetadataEntry meta{};
    strncpy(meta.title, "Scanning...", sizeof(meta.title) - 1);

    RuntimeMetadata r{};
    strncpy(r.path, path, sizeof(r.path) - 1);
    r.path[sizeof(r.path)-1] = '\0';
    r.meta = meta;
    g_flacPlaylistMeta.push_back(r);

    printf("[FLAC] Adding to playlist (localIdx=%d): %s\n", localIndex, path);

    // Try cache first
    auto it = g_flacCache.find(path);
    if (it != g_flacCache.end() && flacCacheValid(it->second))
    {
        mutexLock(&g_flacMetaMutex);
        g_flacPlaylistMeta[localIndex].meta = it->second.meta;
        mutexUnlock(&g_flacMetaMutex);
        printf("[FLAC] Cache hit: %s\n", path);
        return true;
    }

    // Queue background scan using the LOCAL index
    mutexLock(&g_flacScanMutex);
    if (g_flacScanQueued.insert(path).second)
        g_flacScanQueue.push_back({ path, localIndex, g_flacScanGeneration });
    mutexUnlock(&g_flacScanMutex);

    return true;
}

// Look up FLAC metadata by path rather than global playlist index,
// so it works correctly in mixed MP3+FLAC playlists.
const Mp3MetadataEntry* flacGetTrackMetadata(int globalIndex)
{
    if (globalIndex < 0 || globalIndex >= playlistGetCount()) return nullptr;
    const char* path = playlistGetTrack(globalIndex);
    if (!path) return nullptr;

    for (auto& r : g_flacPlaylistMeta)
        if (strcmp(r.path, path) == 0)
            return &r.meta;

    return nullptr;
}

int flacGetPlaylistCount()
{
    return (int)g_flacPlaylistMeta.size();
}

/* =======================================================
   DECODER  (used by player.cpp at playback time)
======================================================= */

/* -------------------------------------------------------
   libFLAC callbacks
------------------------------------------------------- */
static FLAC__StreamDecoderWriteStatus flacWriteCallback(
    const FLAC__StreamDecoder* /*decoder*/,
    const FLAC__Frame*          frame,
    const FLAC__int32* const*   buffer,
    void*                       clientData)
{
    FlacDecoder* fd = (FlacDecoder*)clientData;
    if (!fd || fd->error) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;

    uint32_t blockSize = frame->header.blocksize;
    uint32_t ch        = frame->header.channels;
    uint32_t bps       = frame->header.bits_per_sample;

    // Clamp to stereo — player.cpp expects 2-channel int16
    if (ch > 2) ch = 2;

    // Scale factor: convert bps → int16_t range
    int shift = (int)bps - 16;

    for (uint32_t i = 0; i < blockSize; i++)
    {
        int32_t l = buffer[0][i];
        int32_t r = (frame->header.channels > 1) ? buffer[1][i] : l;

        // Scale to 16-bit
        int16_t sl, sr;
        if (shift > 0)       { sl = (int16_t)(l >> shift); sr = (int16_t)(r >> shift); }
        else if (shift < 0)  { sl = (int16_t)(l << -shift); sr = (int16_t)(r << -shift); }
        else                 { sl = (int16_t)l; sr = (int16_t)r; }

        // Write into ring buffer — drop frames if full (shouldn't happen with
        // the 8192-frame buffer and the read loop draining it promptly)
        if (fd->pcmCount < FlacDecoder::BUF_FRAMES)
        {
            int writePos = fd->pcmTail;
            fd->pcmBuf[writePos * 2]     = sl;
            fd->pcmBuf[writePos * 2 + 1] = sr;
            fd->pcmTail  = (fd->pcmTail + 1) % FlacDecoder::BUF_FRAMES;
            fd->pcmCount++;
            fd->samplesRead++;
        }
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void flacMetadataCallback(
    const FLAC__StreamDecoder* /*decoder*/,
    const FLAC__StreamMetadata* metadata,
    void*                       clientData)
{
    FlacDecoder* fd = (FlacDecoder*)clientData;
    if (!fd) return;

    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
    {
        fd->sampleRate    = metadata->data.stream_info.sample_rate;
        fd->channels      = metadata->data.stream_info.channels;
        fd->bitsPerSample = metadata->data.stream_info.bits_per_sample;
        fd->totalSamples  = metadata->data.stream_info.total_samples;
    }
}

static void flacErrorCallback(
    const FLAC__StreamDecoder* /*decoder*/,
    FLAC__StreamDecoderErrorStatus status,
    void*                          clientData)
{
    FlacDecoder* fd = (FlacDecoder*)clientData;
    if (fd) fd->error = true;
    printf("[FLAC] decoder error: %s\n",
           FLAC__StreamDecoderErrorStatusString[status]);
}

/* -------------------------------------------------------
   flacOpen / flacClose
------------------------------------------------------- */
FlacDecoder* flacOpen(const char* path)
{
    FlacDecoder* fd = new FlacDecoder();

    fd->decoder = FLAC__stream_decoder_new();
    if (!fd->decoder) { delete fd; return nullptr; }

    // Enable MD5 checking is optional; skip for performance on Switch
    FLAC__stream_decoder_set_md5_checking(fd->decoder, false);

    // We want STREAMINFO so the metadata callback fires
    FLAC__stream_decoder_set_metadata_respond(
        fd->decoder, FLAC__METADATA_TYPE_STREAMINFO);

    FLAC__StreamDecoderInitStatus status =
        FLAC__stream_decoder_init_file(
            fd->decoder,
            path,
            flacWriteCallback,
            flacMetadataCallback,
            flacErrorCallback,
            fd
        );

    if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        FLAC__stream_decoder_delete(fd->decoder);
        delete fd;
        return nullptr;
    }

    // Process metadata blocks (fires flacMetadataCallback → sets sampleRate etc.)
    if (!FLAC__stream_decoder_process_until_end_of_metadata(fd->decoder))
    {
        FLAC__stream_decoder_delete(fd->decoder);
        delete fd;
        return nullptr;
    }

    if (fd->sampleRate == 0)
    {
        // Metadata callback never fired — not a valid FLAC
        FLAC__stream_decoder_delete(fd->decoder);
        delete fd;
        return nullptr;
    }

    return fd;
}

void flacClose(FlacDecoder* fd)
{
    if (!fd) return;
    if (fd->decoder)
    {
        FLAC__stream_decoder_finish(fd->decoder);
        FLAC__stream_decoder_delete(fd->decoder);
    }
    delete fd;
}

/* -------------------------------------------------------
   flacRead  — mirrors mpg123_read() for player.cpp
------------------------------------------------------- */
FlacReadResult flacRead(FlacDecoder* fd,
                        unsigned char* buffer,
                        size_t bufBytes,
                        size_t* bytesRead)
{
    *bytesRead = 0;
    if (!fd || fd->error) return FLAC_READ_ERR;

    // How many int16_t stereo frames fit in bufBytes?
    int maxFrames = (int)(bufBytes / (sizeof(int16_t) * 2));
    if (maxFrames <= 0) return FLAC_READ_OK;

    // Decode blocks from libFLAC until the ring buffer has enough data
    // or the stream ends.
    while (fd->pcmCount < maxFrames && !fd->eof && !fd->error)
    {
        FLAC__StreamDecoderState state =
            FLAC__stream_decoder_get_state(fd->decoder);

        if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
        {
            fd->eof = true;
            break;
        }

        if (!FLAC__stream_decoder_process_single(fd->decoder))
        {
            if (FLAC__stream_decoder_get_state(fd->decoder) ==
                FLAC__STREAM_DECODER_END_OF_STREAM)
                fd->eof = true;
            else
                fd->error = true;
            break;
        }
    }

    // Drain ring buffer into caller's buffer
    int framesAvail  = std::min(fd->pcmCount, maxFrames);
    int16_t* outBuf  = (int16_t*)buffer;

    for (int i = 0; i < framesAvail; i++)
    {
        outBuf[i * 2]     = fd->pcmBuf[fd->pcmHead * 2];
        outBuf[i * 2 + 1] = fd->pcmBuf[fd->pcmHead * 2 + 1];
        fd->pcmHead = (fd->pcmHead + 1) % FlacDecoder::BUF_FRAMES;
        fd->pcmCount--;
    }

    *bytesRead = (size_t)(framesAvail * sizeof(int16_t) * 2);

    if (fd->error)                        return FLAC_READ_ERR;
    if (framesAvail == 0 && fd->eof)      return FLAC_READ_DONE;
    return FLAC_READ_OK;
}

/* -------------------------------------------------------
   flacSeek
------------------------------------------------------- */
bool flacSeek(FlacDecoder* fd, uint64_t targetSample)
{
    if (!fd || !fd->decoder || fd->error) return false;

    // Clear the ring buffer — stale data is invalid after seek
    fd->pcmHead  = 0;
    fd->pcmTail  = 0;
    fd->pcmCount = 0;
    fd->eof      = false;
    fd->error    = false;

    if (!FLAC__stream_decoder_seek_absolute(fd->decoder, targetSample))
    {
        // Some files don't support fast seek; reset and re-decode from 0
        FLAC__stream_decoder_reset(fd->decoder);
        FLAC__stream_decoder_process_until_end_of_metadata(fd->decoder);
        fd->samplesRead = 0;
        return false;
    }

    fd->samplesRead = targetSample;
    return true;
}
