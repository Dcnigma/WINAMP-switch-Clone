#include "mp3.h"
#include "playlist.h"
#include <vector>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static std::vector<Mp3MetadataEntry> playlistMetadata;

/* ---------- Helpers ---------- */

// Convert ID3v2 syncsafe integer to int
static unsigned int syncsafeToInt(unsigned char* b)
{
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3];
}

// Read a text frame from ID3v2
static void readTextFrame(FILE* f, unsigned int size, char* out, size_t outSize)
{
    if (size < 1) return;

    unsigned char encoding;
    fread(&encoding, 1, 1, f);
    size--;

    // Only support ISO-8859-1 and UTF-8 safely
    if (encoding == 0 || encoding == 3)
    {
        size_t toRead = (size < outSize - 1) ? size : outSize - 1;
        fread(out, 1, toRead, f);
        out[toRead] = 0;
    }
    else
    {
        // Skip UTF-16 etc to avoid garbage
        strncpy(out, "Unknown", outSize);
        fseek(f, size, SEEK_CUR);
    }
}


// Read ID3v2 metadata from MP3
static void readMp3Metadata(const char* path, Mp3MetadataEntry& entry)
{
    memset(&entry, 0, sizeof(entry));

    FILE* f = fopen(path, "rb");
    if (!f) return;

    unsigned char header[10];
    if (fread(header, 1, 10, f) != 10) { fclose(f); return; }

    // Not an ID3v2 tag → nothing to read
    if (memcmp(header, "ID3", 3) != 0) { fclose(f); return; }

    //  Get total tag size (syncsafe int)
    unsigned int tagSize = syncsafeToInt(&header[6]);

    // Tag size does NOT include header, so add 10
    long tagEnd = 10 + tagSize;

    while (ftell(f) < tagEnd)
    {
        char frameId[5] = {0};
        unsigned char sizeBytes[4];

        if (fread(frameId, 1, 4, f) != 4) break;
        if (frameId[0] == 0) break;

        if (fread(sizeBytes, 1, 4, f) != 4) break;

        // skip flags
        fseek(f, 2, SEEK_CUR);

        unsigned int frameSize =
            (header[4] == 4)
            ? syncsafeToInt(sizeBytes)
            : (sizeBytes[0]<<24)|(sizeBytes[1]<<16)|(sizeBytes[2]<<8)|(sizeBytes[3]);

        if (frameSize == 0) break;

        if (strcmp(frameId,"TIT2")==0)
            readTextFrame(f, frameSize, entry.title, sizeof(entry.title));
        else if (strcmp(frameId,"TPE1")==0)
            readTextFrame(f, frameSize, entry.artist, sizeof(entry.artist));
        else
            fseek(f, frameSize, SEEK_CUR);
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

    int versionIndex = (h[1] >> 3) & 0x03;
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

static int getMp3DurationSeconds(const char* path, int bitrateKbps)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;

    if (bitrateKbps > 0)
        return (int)((st.st_size * 8.0) / (bitrateKbps * 1000.0));

    return 0; // VBR handled earlier via Xing/VBRI
}
//
//
// // --- Get MP3 duration using bitrate (works for CBR, approximate for VBR) ---
// static int getMp3DurationSeconds(const char* path, int bitrateKbps)
// {
//     FILE* f = fopen(path, "rb");
//     if (!f) return 0;
//
//     unsigned char header[10];
//     if (fread(header, 1, 10, f) != 10) { fclose(f); return 0; }
//
//     if (memcmp(header, "ID3", 3) == 0) {
//         int tagSize = (header[6]<<21)|(header[7]<<14)|(header[8]<<7)|header[9];
//         fseek(f, 10 + tagSize, SEEK_SET);
//     } else {
//         fseek(f, 0, SEEK_SET);
//     }
//
//     unsigned char frame[4];
//     if (fread(frame, 1, 4, f) != 4) { fclose(f); return 0; }
//
//     int versionIndex = (frame[1] >> 3) & 0x03;
//     int srIndex      = (frame[2] >> 2) & 0x03;
//
//     static const int sampleRates[2][4] = {
//         {22050,24000,16000,0},
//         {44100,48000,32000,0}
//     };
//
//     int version = (versionIndex == 3) ? 1 : 0;
//     int sampleRate = sampleRates[version][srIndex];
//
//     fseek(f, 32, SEEK_CUR); // jump to possible Xing header area
//
//     char tag[4];
//     fread(tag, 1, 4, f);
//
//     if (memcmp(tag, "Xing", 4) == 0 || memcmp(tag, "Info", 4) == 0)
//     {
//         unsigned char flags[4];
//         fread(flags, 1, 4, f);
//
//         if (flags[3] & 0x01) // frames field present
//         {
//             unsigned char frames[4];
//             fread(frames, 1, 4, f);
//             int totalFrames = (frames[0]<<24)|(frames[1]<<16)|(frames[2]<<8)|frames[3];
//
//             fclose(f);
//             return (int)((double)totalFrames * 1152 / sampleRate);
//         }
//     }
//
//     fclose(f);
//
//     // fallback to CBR estimate
//     if (bitrateKbps <= 0) return 0;
//     struct stat st;
//     if (stat(path, &st) != 0) return 0;
//     return (int)((double)st.st_size * 8 / (bitrateKbps * 1000));
// }


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

    // read bitrate & sample rate (zero if unknown)
    readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz, entry.channels);


    // duration using actual bitrate (works for CBR, approximate for VBR)
    entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

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
        readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz, entry.channels);

        entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

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

    entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

    playlistAdd(path);
    playlistMetadata.push_back(entry);

    return true;
}
