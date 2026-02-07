#include "mp3.h"
#include "playlist.h"
#include <vector>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "player.h"
#include <mpg123.h>

static std::vector<Mp3MetadataEntry> playlistMetadata;



/* ---------- Helpers ---------- */

void debugLog(const char* fmt, ...)
{
    static bool initialized = false;
    if (!initialized) {
        socketInitializeDefault();
        nxlinkStdio();
        initialized = true;
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

// Convert ID3v2 syncsafe integer to int
static unsigned int syncsafeToInt(unsigned char* b)
{
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3];
}

// Read a text frame from ID3v2
static void readTextFrame(FILE* f, unsigned int size, char* out, size_t outSize)
{
    if (size < 1 || outSize == 0) return;

    unsigned char encoding;
    fread(&encoding, 1, 1, f);
    size--;

    if (encoding == 0) // ISO-8859-1 / ASCII
    {
        size_t toRead = (size < outSize - 1) ? size : outSize - 1;
        fread(out, 1, toRead, f);
        out[toRead] = 0;

        if (size > toRead)
            fseek(f, size - toRead, SEEK_CUR);
    }
    else if (encoding == 3) // UTF-8
    {
        size_t toRead = (size < outSize - 1) ? size : outSize - 1;
        fread(out, 1, toRead, f);
        out[toRead] = 0;

        if (size > toRead)
            fseek(f, size - toRead, SEEK_CUR);
    }
    else if (encoding == 1 || encoding == 2) // UTF-16 (with or without BOM)
    {
        // Read full UTF-16 string into temp buffer
        unsigned char buf[512];
        size_t toRead = (size < sizeof(buf)) ? size : sizeof(buf);
        fread(buf, 1, toRead, f);

        bool bigEndian = (encoding == 2);

        // Check BOM for encoding==1
        size_t offset = 0;
        if (encoding == 1 && toRead >= 2)
        {
            if (buf[0] == 0xFF && buf[1] == 0xFE) { bigEndian = false; offset = 2; }
            else if (buf[0] == 0xFE && buf[1] == 0xFF) { bigEndian = true; offset = 2; }
        }

        // Convert UTF-16 → UTF-8 (basic BMP only, enough for tags)
        size_t outPos = 0;
        for (size_t i = offset; i + 1 < toRead && outPos < outSize - 1; i += 2)
        {
            unsigned short ch = bigEndian
                ? (buf[i] << 8) | buf[i+1]
                : (buf[i+1] << 8) | buf[i];

            if (ch == 0) break; // string end

            if (ch < 0x80)
            {
                out[outPos++] = (char)ch;
            }
            else if (ch < 0x800 && outPos < outSize - 2)
            {
                out[outPos++] = 0xC0 | (ch >> 6);
                out[outPos++] = 0x80 | (ch & 0x3F);
            }
            else if (outPos < outSize - 3)
            {
                out[outPos++] = 0xE0 | (ch >> 12);
                out[outPos++] = 0x80 | ((ch >> 6) & 0x3F);
                out[outPos++] = 0x80 | (ch & 0x3F);
            }
        }

        out[outPos] = 0;

        if (size > toRead)
            fseek(f, size - toRead, SEEK_CUR);
    }
    else
    {
        // Unknown encoding → skip
        fseek(f, size, SEEK_CUR);
        out[0] = 0;
    }
}

static void readID3v1Fallback(const char* path, Mp3MetadataEntry& entry)
{
    if (entry.artist[0] && entry.title[0]) return;

    FILE* f = fopen(path, "rb");
    if (!f) return;

    if (fseek(f, -128, SEEK_END) != 0) { fclose(f); return; }

    char tag[128];
    if (fread(tag, 1, 128, f) != 128) { fclose(f); return; }

    if (memcmp(tag, "TAG", 3) == 0)
    {
        if (entry.title[0] == 0)
        {
            memcpy(entry.title, tag + 3, 30);
            entry.title[30] = 0;
        }

        if (entry.artist[0] == 0)
        {
            memcpy(entry.artist, tag + 33, 30);
            entry.artist[30] = 0;
        }
    }

    fclose(f);
}

// Read ID3v2 metadata from MP3
static void readMp3Metadata(const char* path, Mp3MetadataEntry& entry)
{
    memset(&entry, 0, sizeof(entry));

    FILE* f = fopen(path, "rb");
    if (!f) return;

    unsigned char header[10];
    if (fread(header, 1, 10, f) != 10) { fclose(f); return; }

    if (memcmp(header, "ID3", 3) != 0) { fclose(f); return; }

    int version = header[3];   // 2, 3 or 4
    unsigned int tagSize = syncsafeToInt(&header[6]);
    long tagEnd = 10 + tagSize;

    printf("ID3v2.%d tag detected in %s\n", version, path);

    while (ftell(f) < tagEnd)
    {
        if (version == 2)
        {
            // -------- ID3v2.2 (3-char frame IDs) --------
            char frameId[4] = {0};
            unsigned char sizeBytes[3];

            if (fread(frameId, 1, 3, f) != 3) break;
            if (frameId[0] == 0) break;

            if (fread(sizeBytes, 1, 3, f) != 3) break;

            unsigned int frameSize =
                (sizeBytes[0] << 16) | (sizeBytes[1] << 8) | sizeBytes[2];

            printf("Frame (v2.2): %s Size: %u\n", frameId, frameSize);

            if (frameSize == 0) break;

            if (strcmp(frameId,"TT2")==0)           // Title
                readTextFrame(f, frameSize, entry.title, sizeof(entry.title));
            else if (strcmp(frameId,"TP1")==0)      // Artist
                readTextFrame(f, frameSize, entry.artist, sizeof(entry.artist));
            else if (strcmp(frameId,"TP2")==0 && entry.artist[0]==0) // Album artist fallback
                readTextFrame(f, frameSize, entry.artist, sizeof(entry.artist));
            else
                fseek(f, frameSize, SEEK_CUR);
        }
        else
        {
            // -------- ID3v2.3 / 2.4 (4-char IDs) --------
            char frameId[5] = {0};
            unsigned char sizeBytes[4];

            if (fread(frameId, 1, 4, f) != 4) break;
            if (frameId[0] == 0) break;

            if (fread(sizeBytes, 1, 4, f) != 4) break;

            fseek(f, 2, SEEK_CUR); // flags

            unsigned int frameSize =
                (version == 4)
                ? syncsafeToInt(sizeBytes)
                : (sizeBytes[0]<<24)|(sizeBytes[1]<<16)|(sizeBytes[2]<<8)|sizeBytes[3];

            printf("Frame: %s Size: %u\n", frameId, frameSize);

            if (frameSize == 0) break;

            if (strcmp(frameId,"TIT2")==0)
                readTextFrame(f, frameSize, entry.title, sizeof(entry.title));
            else if (strcmp(frameId,"TPE1")==0)
                readTextFrame(f, frameSize, entry.artist, sizeof(entry.artist));
            else if (strcmp(frameId,"TPE2")==0 && entry.artist[0]==0)
                readTextFrame(f, frameSize, entry.artist, sizeof(entry.artist));
            else
                fseek(f, frameSize, SEEK_CUR);
        }
    }

    fclose(f);
}


// --- Read bitrate & sample rate from first MPEG frame ---
static int readBigEndian32(unsigned char* b)
{
    return (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
}

static void readMp3BitrateAndRate(const char* path,
                                  int& outBitrateKbps,
                                  int& outSampleRateKHz,
                                  int& outChannels)
{
    outBitrateKbps = 0;
    outSampleRateKHz = 0;
    outChannels = 0;

    FILE* f = fopen(path, "rb");
    if (!f) return;

    unsigned char h[4];

    // --- Skip ID3v2 ---
    unsigned char id3[10];
    if (fread(id3,1,10,f)==10 && memcmp(id3,"ID3",3)==0)
    {
        int tagSize = (id3[6]<<21)|(id3[7]<<14)|(id3[8]<<7)|id3[9];
        fseek(f, tagSize, SEEK_CUR);
    }
    else
    {
        fseek(f, 0, SEEK_SET);
    }

    // --- Find MPEG frame ---
    while (fread(h,1,4,f)==4)
    {
        if (h[0]==0xFF && (h[1]&0xE0)==0xE0)
            break;
        fseek(f,-3,SEEK_CUR);
    }

    if (!(h[0]==0xFF && (h[1]&0xE0)==0xE0))
    {
        fclose(f);
        return;
    }

//    int versionIndex = (h[1] >> 3) & 0x03;
    int srIndex      = (h[2] >> 2) & 0x03;

    static const int sampleRates[4] = {44100,48000,32000,0};
    outSampleRateKHz = sampleRates[srIndex] / 1000;
    outChannels = ((h[3] & 0xC0) == 0xC0) ? 1 : 2;

    long frameStart = ftell(f) - 4;

    // Jump into frame to check for Xing/VBRI
    fseek(f, frameStart + 32, SEEK_SET); // typical offset for MPEG1 stereo
    unsigned char tag[4];
    fread(tag,1,4,f);

    // ---------- XING / INFO ----------
    if (memcmp(tag,"Xing",4)==0 || memcmp(tag,"Info",4)==0)
    {
        unsigned char flags[4];
        fread(flags,1,4,f);
        int flagsVal = readBigEndian32(flags);

        int frames = 0;
        int bytes  = 0;

        if (flagsVal & 0x1) { unsigned char b[4]; fread(b,1,4,f); frames = readBigEndian32(b); }
        if (flagsVal & 0x2) { unsigned char b[4]; fread(b,1,4,f); bytes  = readBigEndian32(b); }

        if (frames > 0 && bytes > 0 && outSampleRateKHz > 0)
        {
            double duration = (frames * 1152.0) / (outSampleRateKHz * 1000);
            outBitrateKbps = (int)((bytes * 8.0) / duration / 1000.0);
        }

        fclose(f);
        return;
    }

    // ---------- VBRI ----------
    fseek(f, frameStart + 36, SEEK_SET);
    fread(tag,1,4,f);
    if (memcmp(tag,"VBRI",4)==0)
    {
        fseek(f, 10, SEEK_CUR);
        unsigned char b[4];
        fread(b,1,4,f);
        int bytes = readBigEndian32(b);

        fread(b,1,4,f);
        int frames = readBigEndian32(b);

        if (frames > 0 && bytes > 0 && outSampleRateKHz > 0)
        {
            double duration = (frames * 1152.0) / (outSampleRateKHz * 1000);
            outBitrateKbps = (int)((bytes * 8.0) / duration / 1000.0);
        }

        fclose(f);
        return;
    }

    // ---------- Fallback CBR ----------
    fseek(f, frameStart, SEEK_SET);
    fread(h,1,4,f);

    int brIndex = (h[2] >> 4) & 0x0F;
    static const int brTable[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};

    outBitrateKbps = brTable[brIndex];

    fclose(f);
}

int getMp3DurationSeconds(const char* path, int bitrateKbps)
{
    // FAST PATH — works for most CBR files
    struct stat st;
    if (bitrateKbps > 0 && stat(path, &st) == 0)
    {
        return (int)((st.st_size * 8.0) / (bitrateKbps * 1000.0));
    }

    // SLOW PATH — only for problematic files
    mpg123_handle* mh = mpg123_new(NULL, NULL);
    if (!mh) return 0;

    if (mpg123_open(mh, path) != MPG123_OK)
    {
        mpg123_delete(mh);
        return 0;
    }

    mpg123_scan(mh);  // heavy, but now rare

    off_t samples = mpg123_length(mh);
    long rate;
    int ch, enc;
    mpg123_getformat(mh, &rate, &ch, &enc);

    mpg123_close(mh);
    mpg123_delete(mh);

    if (samples <= 0 || rate <= 0) return 0;

    return samples / rate;
}


/* ---------- Public API ---------- */

void mp3ClearMetadata()
{
    playlistMetadata.clear();
}

bool mp3AddToPlaylist(const char* path)
{
    if (!path) return false;

    playlistAdd(path);

    Mp3MetadataEntry entry;
    readMp3Metadata(path, entry);
    readID3v1Fallback(path, entry);

    // read bitrate & sample rate (zero if unknown)
    readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz, entry.channels);


    // duration using actual bitrate (works for CBR, approximate for VBR)

    // after readMp3BitrateAndRate(...)
    entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

    // 🔍 If duration looks suspicious, force accurate scan
    if (entry.durationSeconds < 5 || entry.durationSeconds > 3600)
    {
        entry.durationSeconds = getMp3DurationSeconds(path, 0); // forces mpg123 scan
    }


    playlistMetadata.push_back(entry);
    return true;
}

void mp3ReloadAllMetadata()
{
    playlistMetadata.clear();

    int count = playlistGetCount();
    for (int i = 0; i < count; i++)
    {
        const char* path = playlistGetTrack(i);
        if (!path) continue;

        Mp3MetadataEntry entry;
        readMp3Metadata(path, entry);
        readID3v1Fallback(path, entry);
        readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz, entry.channels);
        // after readMp3BitrateAndRate(...)
        entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

        // 🔍 If duration looks suspicious, force accurate scan
        if (entry.durationSeconds < 5 || entry.durationSeconds > 3600)
        {
            entry.durationSeconds = getMp3DurationSeconds(path, 0); // forces mpg123 scan
        }


        playlistMetadata.push_back(entry);
    }
}

const Mp3MetadataEntry* mp3GetTrackMetadata(int index)
{
    if (index < 0) return nullptr;
    if (index >= (int)playlistMetadata.size()) return nullptr;
    if (index >= playlistGetCount()) return nullptr;
    return &playlistMetadata[index];
}


int mp3GetPlaylistCount()
{
    return playlistMetadata.size();
}

bool mp3Load(const char* path)
{
    if (!path) return false;

    Mp3MetadataEntry entry;
    readMp3Metadata(path, entry);
    readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz, entry.channels);

    // after readMp3BitrateAndRate(...)
    entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

    // 🔍 If duration looks suspicious, force accurate scan
    if (entry.durationSeconds < 5 || entry.durationSeconds > 3600)
    {
        entry.durationSeconds = getMp3DurationSeconds(path, 0); // forces mpg123 scan
    }


    playlistAdd(path);
    playlistMetadata.push_back(entry);

    return true;
}
