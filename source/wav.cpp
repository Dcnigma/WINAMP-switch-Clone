#include "wav.h"
#include "playlist.h"
#include "player.h"
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

/* -------------------------------------------------------
   Cache
------------------------------------------------------- */
#define WAV_CACHE_VERSION  1
#define WAV_CACHE_PATH     "sdmc:/config/winamp/wav_cache.bin"
#define WAV_CACHE_TMP_PATH "sdmc:/config/winamp/wav_cache.bin.tmp"

struct WavCacheHeader { uint32_t magic; uint32_t version; };
struct WavCacheEntry  { char path[512]; time_t mtime; Mp3MetadataEntry meta; };

static std::unordered_map<std::string, WavCacheEntry> g_wavCache;
static bool g_wavCacheLoaded = false;

/* -------------------------------------------------------
   Scanner state
------------------------------------------------------- */
static Thread  g_wavThread;
static bool    g_wavThreadRunning = false;
static Mutex   g_wavMetaMutex;
static Mutex   g_wavScanMutex;
static bool    g_wavMutexInited   = false;
static char    g_wavLoadedFolder[512] = {0};

struct WavScanJob { std::string path; int localIndex; int generation; };

static std::vector<WavScanJob>         g_wavScanQueue;
static std::unordered_set<std::string> g_wavScanQueued;
static std::vector<RuntimeMetadata>    g_wavPlaylistMeta;
static int                             g_wavScanGeneration = 0;

/* -------------------------------------------------------
   Helpers
------------------------------------------------------- */
static void wavEnsureMutexInited()
{
    if (!g_wavMutexInited)
    {
        mutexInit(&g_wavMetaMutex);
        mutexInit(&g_wavScanMutex);
        g_wavMutexInited = true;
    }
}

static bool wavCacheValid(const WavCacheEntry& e)
{
    struct stat st;
    if (stat(e.path, &st) != 0) return false;
    return st.st_mtime == e.mtime;
}

static bool wavPlaylistHasPath(const char* path)
{
    for (auto& m : g_wavPlaylistMeta)
        if (strcmp(m.path, path) == 0) return true;
    return false;
}

static void ensureCacheDir()
{
    mkdir("sdmc:/config",        0777);
    mkdir("sdmc:/config/winamp", 0777);
}

/* -------------------------------------------------------
   RIFF/WAV header parser
   Handles the common subset of WAV:
     - PCM (format 1)  — 8, 16, 24, 32-bit
     - IEEE float (format 3) — 32-bit
     - WAVE_FORMAT_EXTENSIBLE (format 0xFFFE) — falls back to sub-format
   Skips unknown chunks so files with metadata chunks (LIST,
   id3 , bext, etc.) parse correctly.
------------------------------------------------------- */
static uint16_t readU16LE(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t readU32LE(const uint8_t* p) { return (uint32_t)(p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24)); }

static bool parseWavHeader(FILE* f, WavDecoder* wd)
{
    // RIFF header: "RIFF" <fileSize:4> "WAVE"
    uint8_t riff[12];
    if (fread(riff, 1, 12, f) != 12)            return false;
    if (memcmp(riff,     "RIFF", 4) != 0 &&
        memcmp(riff,     "RF64", 4) != 0)        return false;
    if (memcmp(riff + 8, "WAVE", 4) != 0)        return false;

    bool fmtFound  = false;
    bool dataFound = false;

    // Walk chunks until we have both fmt and data
    while (!dataFound)
    {
        uint8_t chunkHdr[8];
        if (fread(chunkHdr, 1, 8, f) != 8) break;

        char     id[5]  = {0};
        memcpy(id, chunkHdr, 4);
        uint32_t size = readU32LE(chunkHdr + 4);

        if (memcmp(id, "fmt ", 4) == 0)
        {
            if (size < 16) return false;
            uint8_t fmt[40] = {0};
            size_t toRead = size < sizeof(fmt) ? size : sizeof(fmt);
            if (fread(fmt, 1, toRead, f) != toRead) return false;
            if (size > toRead) fseek(f, (long)(size - toRead), SEEK_CUR);

            uint16_t audioFmt = readU16LE(fmt);

            // WAVE_FORMAT_EXTENSIBLE — read sub-format from bytes 24..25
            if (audioFmt == 0xFFFE && size >= 26)
                audioFmt = readU16LE(fmt + 24);

            wd->audioFormat   = audioFmt;
            wd->channels      = readU16LE(fmt + 2);
            wd->sampleRate    = readU32LE(fmt + 4);
            wd->bitsPerSample = readU16LE(fmt + 14);

            fmtFound = true;
        }
        else if (memcmp(id, "data", 4) == 0)
        {
            if (!fmtFound) return false;
            wd->dataOffset = ftell(f);
            wd->dataBytes  = size;

            uint32_t bytesPerFrame =
                (wd->bitsPerSample / 8) * wd->channels;
            wd->totalSamples =
                (bytesPerFrame > 0) ? (size / bytesPerFrame) : 0;

            dataFound = true;
            // leave file pointer at start of audio data
        }
        else
        {
            // Unknown chunk — skip it (pad to even boundary)
            long skip = (long)size + (size & 1);
            fseek(f, skip, SEEK_CUR);
        }
    }

    return fmtFound && dataFound;
}

/* -------------------------------------------------------
   Metadata reader — no tags in raw WAV, but we can still
   derive title from filename and duration from the header.
   Some WAVs have an INFO LIST chunk with INAM/IART — we
   read those too if present.
------------------------------------------------------- */
static bool readListInfo(FILE* f, long fileSize,
                         Mp3MetadataEntry& entry)
{
    // Scan for "LIST" chunk with "INFO" sub-type
    fseek(f, 12, SEEK_SET);
    while (ftell(f) < fileSize - 8)
    {
        uint8_t hdr[8];
        if (fread(hdr, 1, 8, f) != 8) break;
        char     id[5] = {0}; memcpy(id, hdr, 4);
        uint32_t size  = readU32LE(hdr + 4);

        if (memcmp(id, "LIST", 4) == 0 && size >= 4)
        {
            char subtype[5] = {0};
            if (fread(subtype, 1, 4, f) != 4) break;
            long remaining = (long)size - 4;

            if (memcmp(subtype, "INFO", 4) == 0)
            {
                long end = ftell(f) + remaining;
                while (ftell(f) < end - 8)
                {
                    uint8_t ih[8];
                    if (fread(ih, 1, 8, f) != 8) break;
                    char     iid[5]  = {0}; memcpy(iid, ih, 4);
                    uint32_t isize   = readU32LE(ih + 4);
                    uint32_t toRead  = isize < 127 ? isize : 127;
                    char     buf[128] = {0};
                    if (fread(buf, 1, toRead, f) != toRead) break;
                    if (isize > toRead) fseek(f, (long)(isize - toRead), SEEK_CUR);
                    if (isize & 1) fseek(f, 1, SEEK_CUR); // pad

                    if (memcmp(iid, "INAM", 4) == 0 && entry.title[0] == '\0')
                        strncpy(entry.title, buf, sizeof(entry.title) - 1);
                    else if (memcmp(iid, "IART", 4) == 0 && entry.artist[0] == '\0')
                        strncpy(entry.artist, buf, sizeof(entry.artist) - 1);
                }
                return true;
            }
            else
            {
                fseek(f, remaining, SEEK_CUR);
            }
        }
        else
        {
            long skip = (long)size + (size & 1);
            fseek(f, skip, SEEK_CUR);
        }
    }
    return false;
}

static void readWavMetadata(const char* path, Mp3MetadataEntry& entry)
{
    memset(&entry, 0, sizeof(entry));
    entry.replayGainPeak      = 1.0f;
    entry.replayGainAlbumPeak = 1.0f;

    FILE* f = fopen(path, "rb");
    if (!f)
    {
        // Fallback: filename as title
        const char* slash = strrchr(path, '/');
        const char* name  = slash ? slash + 1 : path;
        strncpy(entry.title, name, sizeof(entry.title) - 1);
        return;
    }

    WavDecoder wd{};
    if (parseWavHeader(f, &wd))
    {
        entry.sampleRateKHz = (int)(wd.sampleRate / 1000);
        entry.channels      = (int)wd.channels;

        // PCM bitrate = sampleRate * channels * bitsPerSample
        entry.bitrateKbps = (int)(
            (uint64_t)wd.sampleRate * wd.channels * wd.bitsPerSample / 1000);

        if (wd.sampleRate > 0 && wd.totalSamples > 0)
            entry.durationSeconds = (int)(wd.totalSamples / wd.sampleRate);
    }

    struct stat st;
    long fileSize = (stat(path, &st) == 0) ? (long)st.st_size : 0;

    // Try to read LIST INFO chunk for embedded title/artist
    readListInfo(f, fileSize, entry);
    fclose(f);

    // Filename fallback
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
   Cache I/O
------------------------------------------------------- */
void wavLoadCache(const char* /*folderKey*/)
{
    if (g_wavCacheLoaded) return;
    ensureCacheDir();

    FILE* f = fopen(WAV_CACHE_PATH, "rb");
    if (!f) { g_wavCacheLoaded = true; return; }

    WavCacheHeader h{};
    if (fread(&h, sizeof(h), 1, f) != 1 ||
        h.magic   != 0x57415643 ||  // 'WAVC'
        h.version != WAV_CACHE_VERSION)
    { fclose(f); g_wavCacheLoaded = true; return; }

    WavCacheEntry e;
    while (fread(&e, sizeof(e), 1, f) == 1)
        if (wavCacheValid(e))
            g_wavCache[e.path] = e;

    fclose(f);
    g_wavCacheLoaded = true;
}

static void wavAppendCache(const char* path, const Mp3MetadataEntry& meta)
{
    if (!path || strncmp(path, "romfs:/", 7) == 0) return;
    if (strstr(path, ".tmp")) return;

    struct stat st;
    if (stat(path, &st) != 0) return;

    static time_t lastWrite = 0;
    time_t now = time(nullptr);
    if (now - lastWrite < 1) return;
    lastWrite = now;

    WavCacheEntry& e = g_wavCache[path];
    strncpy(e.path, path, sizeof(e.path) - 1);
    e.path[sizeof(e.path)-1] = '\0';
    e.meta  = meta;
    e.mtime = st.st_mtime;

    FILE* f = fopen(WAV_CACHE_TMP_PATH, "wb");
    if (!f) return;

    WavCacheHeader h{ 0x57415643, WAV_CACHE_VERSION };
    if (fwrite(&h, sizeof(h), 1, f) != 1) goto fail;
    for (auto& [_, entry] : g_wavCache)
        if (fwrite(&entry, sizeof(entry), 1, f) != 1) goto fail;

    fflush(f); fsync(fileno(f)); fclose(f);
    remove(WAV_CACHE_PATH);
    rename(WAV_CACHE_TMP_PATH, WAV_CACHE_PATH);
    return;
fail:
    fclose(f); remove(WAV_CACHE_TMP_PATH);
}

/* -------------------------------------------------------
   Background scanner
------------------------------------------------------- */
static void wavScanThread(void*)
{
    while (g_wavThreadRunning)
    {
        WavScanJob job;

        mutexLock(&g_wavScanMutex);
        if (g_wavScanQueue.empty())
        {
            mutexUnlock(&g_wavScanMutex);
            svcSleepThread(50'000'000);
            continue;
        }
        job = g_wavScanQueue.front();
        g_wavScanQueue.erase(g_wavScanQueue.begin());
        mutexUnlock(&g_wavScanMutex);

        mutexLock(&g_wavScanMutex);
        bool canceled = (job.generation != g_wavScanGeneration);
        mutexUnlock(&g_wavScanMutex);
        if (canceled) continue;

        if (playerIsPlaying())
        {
            const char* playing = playlistGetTrack(playlistGetCurrentIndex());
            if (playing && job.path == playing) continue;
        }

        Mp3MetadataEntry entry{};
        readWavMetadata(job.path.c_str(), entry);

        mutexLock(&g_wavMetaMutex);
        if (job.localIndex < (int)g_wavPlaylistMeta.size())
            g_wavPlaylistMeta[job.localIndex].meta = entry;
        mutexUnlock(&g_wavMetaMutex);

        wavAppendCache(job.path.c_str(), entry);
    }
}

/* -------------------------------------------------------
   Public metadata / playlist API
------------------------------------------------------- */
bool wavIsFolderLoaded(const char* path)
{
    return path && strcmp(g_wavLoadedFolder, path) == 0;
}

void wavSetLoadedFolder(const char* path)
{
    if (!path) return;
    strncpy(g_wavLoadedFolder, path, sizeof(g_wavLoadedFolder) - 1);
    g_wavLoadedFolder[sizeof(g_wavLoadedFolder)-1] = '\0';
}

void wavCancelAllScans()
{
    wavEnsureMutexInited();
    mutexLock(&g_wavScanMutex);
    g_wavScanQueue.clear();
    g_wavScanQueued.clear();
    g_wavScanGeneration++;
    mutexUnlock(&g_wavScanMutex);
}

void wavStartBackgroundScanner()
{
    wavEnsureMutexInited();
    g_wavThreadRunning = true;
    threadCreate(&g_wavThread, wavScanThread, nullptr, nullptr, 0x4000, 0x2B, -2);
    threadStart(&g_wavThread);
}

void wavStopBackgroundScanner()
{
    if (!g_wavThreadRunning) return;
    g_wavThreadRunning = false;
    threadWaitForExit(&g_wavThread);
    threadClose(&g_wavThread);
}

void wavClearMetadata()
{
    wavEnsureMutexInited();
    mutexLock(&g_wavMetaMutex);
    g_wavPlaylistMeta.clear();
    mutexUnlock(&g_wavMetaMutex);
}

bool wavAddToPlaylist(const char* path)
{
    if (!path) return false;
    wavEnsureMutexInited();
    if (wavPlaylistHasPath(path)) return false;

    playlistAdd(path);
    int localIndex = (int)g_wavPlaylistMeta.size();

    Mp3MetadataEntry meta{};
    strncpy(meta.title, "Scanning...", sizeof(meta.title) - 1);

    RuntimeMetadata r{};
    strncpy(r.path, path, sizeof(r.path) - 1);
    r.path[sizeof(r.path)-1] = '\0';
    r.meta = meta;
    g_wavPlaylistMeta.push_back(r);

    auto it = g_wavCache.find(path);
    if (it != g_wavCache.end() && wavCacheValid(it->second))
    {
        mutexLock(&g_wavMetaMutex);
        g_wavPlaylistMeta[localIndex].meta = it->second.meta;
        mutexUnlock(&g_wavMetaMutex);
        printf("[WAV] Cache hit: %s\n", path);
        return true;
    }

    mutexLock(&g_wavScanMutex);
    if (g_wavScanQueued.insert(path).second)
        g_wavScanQueue.push_back({ path, localIndex, g_wavScanGeneration });
    mutexUnlock(&g_wavScanMutex);

    return true;
}

const Mp3MetadataEntry* wavGetTrackMetadata(int globalIndex)
{
    if (globalIndex < 0 || globalIndex >= playlistGetCount()) return nullptr;
    const char* path = playlistGetTrack(globalIndex);
    if (!path) return nullptr;
    for (auto& r : g_wavPlaylistMeta)
        if (strcmp(r.path, path) == 0) return &r.meta;
    return nullptr;
}

int wavGetPlaylistCount() { return (int)g_wavPlaylistMeta.size(); }

/* =======================================================
   DECODER
======================================================= */
WavDecoder* wavOpen(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;

    WavDecoder* wd = new WavDecoder();
    wd->file = f;

    if (!parseWavHeader(f, wd))
    {
        fclose(f);
        delete wd;
        return nullptr;
    }

    // Validate: only PCM (1) and IEEE float (3) supported
    if (wd->audioFormat != 1 && wd->audioFormat != 3)
    {
        printf("[WAV] Unsupported audio format: %u in %s\n", wd->audioFormat, path);
        fclose(f);
        delete wd;
        return nullptr;
    }

    if (wd->channels == 0 || wd->sampleRate == 0 || wd->bitsPerSample == 0)
    {
        fclose(f);
        delete wd;
        return nullptr;
    }

    // Seek to start of audio data (parseWavHeader leaves us there already,
    // but store the offset so wavSeek can jump back)
    wd->dataOffset = ftell(f);

    return wd;
}

void wavClose(WavDecoder* wd)
{
    if (!wd) return;
    if (wd->file) fclose(wd->file);
    delete wd;
}

/* -------------------------------------------------------
   Convert one source frame to stereo int16_t output.
   src points to the start of one interleaved PCM frame
   in the native format. Returns two samples: [left, right].
------------------------------------------------------- */
static void convertFrame(const uint8_t* src,
                         uint32_t channels,
                         uint32_t bitsPerSample,
                         uint32_t audioFormat,
                         int16_t& outL, int16_t& outR)
{
    auto sampleToI16 = [&](const uint8_t* p) -> int16_t
    {
        if (audioFormat == 3 && bitsPerSample == 32)
        {
            // IEEE float 32-bit
            float v;
            memcpy(&v, p, 4);
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            return (int16_t)(v * 32767.0f);
        }
        switch (bitsPerSample)
        {
            case 8:  // WAV 8-bit is unsigned
                return (int16_t)((int)(p[0]) - 128) << 8;
            case 16:
                return (int16_t)(p[0] | (p[1] << 8));
            case 24:
            {
                int32_t v = (int32_t)(p[0] | (p[1] << 8) | ((int8_t)p[2] << 16));
                return (int16_t)(v >> 8);
            }
            case 32:
            {
                int32_t v;
                memcpy(&v, p, 4);
                return (int16_t)(v >> 16);
            }
            default:
                return 0;
        }
    };

    uint32_t bytesPerSample = bitsPerSample / 8;
    outL = sampleToI16(src);
    outR = (channels > 1) ? sampleToI16(src + bytesPerSample) : outL;
}

WavReadResult wavRead(WavDecoder* wd,
                      unsigned char* buffer,
                      size_t         bufBytes,
                      size_t*        bytesRead)
{
    *bytesRead = 0;
    if (!wd || !wd->file || wd->eof) return WAV_READ_DONE;

    // How many output stereo int16 frames fit in buffer?
    int outFramesMax  = (int)(bufBytes / (sizeof(int16_t) * 2));
    if (outFramesMax == 0) return WAV_READ_OK;

    uint32_t srcBytesPerFrame = (wd->bitsPerSample / 8) * wd->channels;
    if (srcBytesPerFrame == 0) return WAV_READ_ERR;

    // How many source frames are left in the data chunk?
    uint64_t framesLeft = (wd->totalSamples > wd->samplesRead)
                          ? (wd->totalSamples - wd->samplesRead) : 0;
    int framesToRead = (int)std::min((uint64_t)outFramesMax, framesLeft);

    if (framesToRead == 0)
    {
        wd->eof = true;
        return WAV_READ_DONE;
    }

    // Read raw source bytes
    std::vector<uint8_t> srcBuf((size_t)framesToRead * srcBytesPerFrame);
    size_t got = fread(srcBuf.data(), 1, srcBuf.size(), wd->file);
    int framesGot = (int)(got / srcBytesPerFrame);

    if (framesGot == 0)
    {
        wd->eof = true;
        return WAV_READ_DONE;
    }

    // Convert to stereo int16_t in output buffer
    int16_t* out = (int16_t*)buffer;
    for (int i = 0; i < framesGot; i++)
    {
        int16_t l, r;
        convertFrame(srcBuf.data() + i * srcBytesPerFrame,
                     wd->channels, wd->bitsPerSample, wd->audioFormat,
                     l, r);
        out[i * 2]     = l;
        out[i * 2 + 1] = r;
    }

    wd->samplesRead += framesGot;
    *bytesRead = (size_t)(framesGot * sizeof(int16_t) * 2);

    if (wd->samplesRead >= wd->totalSamples)
    {
        wd->eof = true;
        return WAV_READ_DONE;
    }

    return WAV_READ_OK;
}

bool wavSeek(WavDecoder* wd, uint64_t targetSample)
{
    if (!wd || !wd->file) return false;

    uint32_t bytesPerFrame = (wd->bitsPerSample / 8) * wd->channels;
    if (bytesPerFrame == 0) return false;

    if (targetSample > wd->totalSamples)
        targetSample = wd->totalSamples;

    long offset = wd->dataOffset + (long)(targetSample * bytesPerFrame);
    if (fseek(wd->file, offset, SEEK_SET) != 0) return false;

    wd->samplesRead = targetSample;
    wd->eof         = (targetSample >= wd->totalSamples);
    return true;
}
