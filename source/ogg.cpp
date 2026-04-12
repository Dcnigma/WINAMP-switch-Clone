#include "ogg.h"
#include "playlist.h"
#include "player.h"
#include <vorbis/vorbisfile.h>
#include <vorbis/codec.h>
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
#define OGG_CACHE_VERSION  1
#define OGG_CACHE_PATH     "sdmc:/config/winamp/ogg_cache.bin"
#define OGG_CACHE_TMP_PATH "sdmc:/config/winamp/ogg_cache.bin.tmp"

struct OggCacheHeader { uint32_t magic; uint32_t version; };
struct OggCacheEntry  { char path[512]; time_t mtime; Mp3MetadataEntry meta; };

static std::unordered_map<std::string, OggCacheEntry> g_oggCache;
static bool g_oggCacheLoaded = false;

/* -------------------------------------------------------
   Scanner state
------------------------------------------------------- */
static Thread  g_oggThread;
static bool    g_oggThreadRunning = false;
static Mutex   g_oggMetaMutex;
static Mutex   g_oggScanMutex;
static bool    g_oggMutexInited   = false;
static char    g_oggLoadedFolder[512] = {0};

struct OggScanJob { std::string path; int localIndex; int generation; };

static std::vector<OggScanJob>         g_oggScanQueue;
static std::unordered_set<std::string> g_oggScanQueued;
static std::vector<RuntimeMetadata>    g_oggPlaylistMeta;
static int                             g_oggScanGeneration = 0;

/* -------------------------------------------------------
   Helpers
------------------------------------------------------- */
static void oggEnsureMutexInited()
{
    if (!g_oggMutexInited)
    {
        mutexInit(&g_oggMetaMutex);
        mutexInit(&g_oggScanMutex);
        g_oggMutexInited = true;
    }
}

static bool oggCacheValid(const OggCacheEntry& e)
{
    struct stat st;
    if (stat(e.path, &st) != 0) return false;
    return st.st_mtime == e.mtime;
}

static bool oggPlaylistHasPath(const char* path)
{
    for (auto& m : g_oggPlaylistMeta)
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
void oggLoadCache(const char* /*folderKey*/)
{
    if (g_oggCacheLoaded) return;
    ensureCacheDir();

    FILE* f = fopen(OGG_CACHE_PATH, "rb");
    if (!f) { g_oggCacheLoaded = true; return; }

    OggCacheHeader h{};
    if (fread(&h, sizeof(h), 1, f) != 1 ||
        h.magic   != 0x4F474743 ||   // 'OGGC'
        h.version != OGG_CACHE_VERSION)
    { fclose(f); g_oggCacheLoaded = true; return; }

    OggCacheEntry e;
    while (fread(&e, sizeof(e), 1, f) == 1)
        if (oggCacheValid(e))
            g_oggCache[e.path] = e;

    fclose(f);
    g_oggCacheLoaded = true;
}

static void oggAppendCache(const char* path, const Mp3MetadataEntry& meta)
{
    if (!path || strncmp(path, "romfs:/", 7) == 0) return;
    if (strstr(path, ".tmp")) return;

    struct stat st;
    if (stat(path, &st) != 0) return;

    // Update in-memory cache entry
    OggCacheEntry& e = g_oggCache[path];
    strncpy(e.path, path, sizeof(e.path) - 1);
    e.path[sizeof(e.path)-1] = '\0';
    e.meta  = meta;
    e.mtime = st.st_mtime;

    // Write the full cache atomically.
    // NOTE: No per-second throttle here — the throttle in flac/wav caused
    // all files scanned within the same second to silently lose their cache
    // entry. OGG folders typically have multiple files scanned back-to-back
    // so every file must be persisted individually.
    FILE* f = fopen(OGG_CACHE_TMP_PATH, "wb");
    if (!f) return;

    OggCacheHeader h{ 0x4F474743, OGG_CACHE_VERSION };
    if (fwrite(&h, sizeof(h), 1, f) != 1) goto fail;
    for (auto& [_, entry] : g_oggCache)
        if (fwrite(&entry, sizeof(entry), 1, f) != 1) goto fail;

    fflush(f); fsync(fileno(f)); fclose(f);
    remove(OGG_CACHE_PATH);
    rename(OGG_CACHE_TMP_PATH, OGG_CACHE_PATH);
    return;
fail:
    fclose(f); remove(OGG_CACHE_TMP_PATH);
}

/* -------------------------------------------------------
   Tag reading
   libvorbisfile makes this trivial — ov_comment() returns
   all Vorbis comments as "KEY=VALUE" UTF-8 strings, same
   format as FLAC.
------------------------------------------------------- */
static void readOggMetadata(const char* path, Mp3MetadataEntry& entry)
{
    memset(&entry, 0, sizeof(entry));
    entry.replayGainPeak      = 1.0f;
    entry.replayGainAlbumPeak = 1.0f;

    OggVorbis_File vf;
    if (ov_fopen(path, &vf) != 0)
    {
        // Fill title from filename as fallback
        const char* slash = strrchr(path, '/');
        const char* name  = slash ? slash + 1 : path;
        const char* dot   = strrchr(name, '.');
        size_t len = dot ? (size_t)(dot - name) : strlen(name);
        if (len >= sizeof(entry.title)) len = sizeof(entry.title) - 1;
        memcpy(entry.title, name, len);
        return;
    }

    // Duration & format
    vorbis_info* vi = ov_info(&vf, -1);
    if (vi)
    {
        entry.sampleRateKHz = (int)(vi->rate / 1000);
        entry.channels      = vi->channels;
        // Ogg Vorbis is VBR; nominal_bitrate is a good estimate
        entry.bitrateKbps   = (vi->bitrate_nominal > 0)
                              ? (int)(vi->bitrate_nominal / 1000) : 0;
    }

    double dur = ov_time_total(&vf, -1);
    if (dur > 0)
    {
        entry.durationSeconds = (int)dur;
    }
    else
    {
        // ov_time_total returns OV_EINVAL (-1) for non-seekable or header-only
        // streams (common with looping game audio). Try ov_pcm_total instead —
        // libvorbisfile will do a full stream scan if needed.
        ogg_int64_t pcmTotal = ov_pcm_total(&vf, -1);
        if (pcmTotal > 0 && vi && vi->rate > 0)
        {
            entry.durationSeconds = (int)(pcmTotal / vi->rate);
        }
        else if (vi && vi->bitrate_nominal > 0)
        {
            // Last resort: estimate from file size and nominal bitrate.
            struct stat st;
            if (stat(path, &st) == 0 && st.st_size > 0)
            {
                // nominal_bitrate is in bits/sec
                entry.durationSeconds =
                    (int)((st.st_size * 8LL) / vi->bitrate_nominal);
            }
        }
    }

    // Tags
    vorbis_comment* vc = ov_comment(&vf, -1);
    if (vc)
    {
        for (int i = 0; i < vc->comments; i++)
        {
            const char* raw = vc->user_comments[i];
            if (!raw) continue;
            const char* eq = strchr(raw, '=');
            if (!eq) continue;

            char key[64] = {0};
            size_t keyLen = (size_t)(eq - raw);
            if (keyLen >= sizeof(key)) keyLen = sizeof(key) - 1;
            memcpy(key, raw, keyLen);

            const char* val = eq + 1;

            if (strcasecmp(key, "TITLE") == 0)
            {
                strncpy(entry.title, val, sizeof(entry.title) - 1);
            }
            else if (strcasecmp(key, "ARTIST") == 0 && entry.artist[0] == '\0')
            {
                strncpy(entry.artist, val, sizeof(entry.artist) - 1);
            }
            else if (strcasecmp(key, "ALBUMARTIST") == 0 && entry.artist[0] == '\0')
            {
                strncpy(entry.artist, val, sizeof(entry.artist) - 1);
            }
            else if (strcasecmp(key, "REPLAYGAIN_TRACK_GAIN") == 0)
            {
                float db = 0.0f;
                if (sscanf(val, "%f", &db) == 1)
                { entry.replayGainDb = db; entry.hasTrackReplayGain = true; }
            }
            else if (strcasecmp(key, "REPLAYGAIN_ALBUM_GAIN") == 0)
            {
                float db = 0.0f;
                if (sscanf(val, "%f", &db) == 1)
                { entry.replayGainAlbumDb = db; entry.hasAlbumReplayGain = true; }
            }
        }
    }

    ov_clear(&vf);

    // Filename fallback for title
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
}

/* -------------------------------------------------------
   Background scanner
------------------------------------------------------- */
static void oggScanThread(void*)
{
    while (g_oggThreadRunning)
    {
        OggScanJob job;

        mutexLock(&g_oggScanMutex);
        if (g_oggScanQueue.empty())
        {
            mutexUnlock(&g_oggScanMutex);
            svcSleepThread(50'000'000);
            continue;
        }
        job = g_oggScanQueue.front();
        g_oggScanQueue.erase(g_oggScanQueue.begin());
        mutexUnlock(&g_oggScanMutex);

        mutexLock(&g_oggScanMutex);
        bool canceled = (job.generation != g_oggScanGeneration);
        mutexUnlock(&g_oggScanMutex);
        if (canceled) continue;

        // Skip the currently playing track
        if (playerIsPlaying())
        {
            const char* playing = playlistGetTrack(playlistGetCurrentIndex());
            if (playing && job.path == playing) continue;
        }

        Mp3MetadataEntry entry{};
        readOggMetadata(job.path.c_str(), entry);

        printf("[OGG] Scanned: %s | dur=%ds kbps=%d\n",
               job.path.c_str(), entry.durationSeconds, entry.bitrateKbps);

        mutexLock(&g_oggMetaMutex);
        if (job.localIndex < (int)g_oggPlaylistMeta.size())
            g_oggPlaylistMeta[job.localIndex].meta = entry;
        mutexUnlock(&g_oggMetaMutex);

        oggAppendCache(job.path.c_str(), entry);
    }
}

/* -------------------------------------------------------
   Public metadata / playlist API
------------------------------------------------------- */
bool oggIsFolderLoaded(const char* path)
{
    return path && strcmp(g_oggLoadedFolder, path) == 0;
}

void oggSetLoadedFolder(const char* path)
{
    if (!path) return;
    strncpy(g_oggLoadedFolder, path, sizeof(g_oggLoadedFolder) - 1);
    g_oggLoadedFolder[sizeof(g_oggLoadedFolder)-1] = '\0';
}

void oggCancelAllScans()
{
    oggEnsureMutexInited();
    mutexLock(&g_oggScanMutex);
    g_oggScanQueue.clear();
    g_oggScanQueued.clear();
    g_oggScanGeneration++;
    mutexUnlock(&g_oggScanMutex);
}

void oggStartBackgroundScanner()
{
    oggEnsureMutexInited();
    g_oggThreadRunning = true;
    threadCreate(&g_oggThread, oggScanThread, nullptr, nullptr, 0x4000, 0x2B, -2);
    threadStart(&g_oggThread);
}

void oggStopBackgroundScanner()
{
    if (!g_oggThreadRunning) return;
    g_oggThreadRunning = false;
    threadWaitForExit(&g_oggThread);
    threadClose(&g_oggThread);
}

void oggClearMetadata()
{
    oggEnsureMutexInited();
    mutexLock(&g_oggMetaMutex);
    g_oggPlaylistMeta.clear();
    mutexUnlock(&g_oggMetaMutex);
}

bool oggAddToPlaylist(const char* path)
{
    if (!path) return false;
    oggEnsureMutexInited();
    if (oggPlaylistHasPath(path)) return false;

    playlistAdd(path);
    int localIndex = (int)g_oggPlaylistMeta.size();

    Mp3MetadataEntry meta{};
    strncpy(meta.title, "Scanning...", sizeof(meta.title) - 1);

    RuntimeMetadata r{};
    strncpy(r.path, path, sizeof(r.path) - 1);
    r.path[sizeof(r.path)-1] = '\0';
    r.meta = meta;
    g_oggPlaylistMeta.push_back(r);

    auto it = g_oggCache.find(path);
    if (it != g_oggCache.end() && oggCacheValid(it->second))
    {
        mutexLock(&g_oggMetaMutex);
        g_oggPlaylistMeta[localIndex].meta = it->second.meta;
        mutexUnlock(&g_oggMetaMutex);
        printf("[OGG] Cache hit: %s\n", path);
        return true;
    }

    mutexLock(&g_oggScanMutex);
    if (g_oggScanQueued.insert(path).second)
        g_oggScanQueue.push_back({ path, localIndex, g_oggScanGeneration });
    mutexUnlock(&g_oggScanMutex);

    return true;
}

const Mp3MetadataEntry* oggGetTrackMetadata(int globalIndex)
{
    if (globalIndex < 0 || globalIndex >= playlistGetCount()) return nullptr;
    const char* path = playlistGetTrack(globalIndex);
    if (!path) return nullptr;
    for (auto& r : g_oggPlaylistMeta)
        if (strcmp(r.path, path) == 0) return &r.meta;
    return nullptr;
}

int oggGetPlaylistCount() { return (int)g_oggPlaylistMeta.size(); }

/* =======================================================
   DECODER
======================================================= */
OggDecoder* oggOpen(const char* path)
{
    OggDecoder* od = new OggDecoder();

    if (ov_fopen(path, &od->vf) != 0)
    {
        delete od;
        return nullptr;
    }

    vorbis_info* vi = ov_info(&od->vf, -1);
    if (!vi || vi->rate == 0)
    {
        ov_clear(&od->vf);
        delete od;
        return nullptr;
    }

    od->sampleRate   = (uint32_t)vi->rate;
    od->channels     = (uint32_t)vi->channels;
    // ov_pcm_total returns OV_EINVAL (-1) for non-seekable streams.
    // Store 0 in that case — player.cpp uses decoderTotalSamples() > 0
    // as the guard, so 0 means "unknown length, rely on MPG123_DONE signal".
    ogg_int64_t pcmTotal = ov_pcm_total(&od->vf, -1);
    od->totalSamples = (pcmTotal > 0) ? (int64_t)pcmTotal : 0;
    od->open         = true;

    return od;
}

void oggClose(OggDecoder* od)
{
    if (!od) return;
    if (od->open) ov_clear(&od->vf);
    delete od;
}

OggReadResult oggRead(OggDecoder* od,
                      unsigned char* buffer,
                      size_t         bufBytes,
                      size_t*        bytesRead)
{
    *bytesRead = 0;
    if (!od || !od->open || od->eof) return OGG_READ_DONE;

    // ov_read expects a signed 16-bit little-endian output
    // bitstream = -1 means "current logical bitstream"
    int  bitstream = 0;
    long totalRead = 0;

    while (totalRead < (long)bufBytes)
    {
        long n = ov_read(&od->vf,
                         (char*)buffer + totalRead,
                         (int)(bufBytes - totalRead),
                         0,    // little-endian
                         2,    // 16-bit
                         1,    // signed
                         &bitstream);

        if (n == 0)
        {
            // End of stream
            od->eof = true;
            break;
        }
        else if (n == OV_HOLE)
        {
            // Recoverable gap in the stream — just retry
            continue;
        }
        else if (n < 0)
        {
            // Unrecoverable error
            if (totalRead == 0) return OGG_READ_ERR;
            break;
        }

        totalRead += n;

        // Update sample counter (n is bytes; int16 stereo = 4 bytes/frame)
        int bytesPerFrame = (int)(sizeof(int16_t) * od->channels);
        if (bytesPerFrame > 0)
            od->samplesRead += n / bytesPerFrame;
    }

    *bytesRead = (size_t)totalRead;

    if (totalRead == 0 && od->eof) return OGG_READ_DONE;
    return OGG_READ_OK;
}

bool oggSeek(OggDecoder* od, uint64_t targetSample)
{
    if (!od || !od->open) return false;
    od->eof = false;
    if (ov_pcm_seek(&od->vf, (ogg_int64_t)targetSample) == 0)
    {
        od->samplesRead = targetSample;
        return true;
    }
    return false;
}
