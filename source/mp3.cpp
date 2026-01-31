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
    fread(&encoding, 1, 1, f); // encoding byte
    size--;

    size_t toRead = (size < outSize - 1) ? size : outSize - 1;
    fread(out, 1, toRead, f);
    out[toRead] = 0;

    if (size > toRead)
        fseek(f, size - toRead, SEEK_CUR);
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

    while (true)
    {
        char frameId[5] = {0};
        unsigned char sizeBytes[4];

        if (fread(frameId, 1, 4, f) != 4) break;
        if (frameId[0] == 0) break;

        if (fread(sizeBytes, 1, 4, f) != 4) break;
        fseek(f, 2, SEEK_CUR);

        unsigned int frameSize =
            (header[3] == 4)
            ? syncsafeToInt(sizeBytes)
            : (sizeBytes[0]<<24)|(sizeBytes[1]<<16)|(sizeBytes[2]<<8)|(sizeBytes[3]);

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
static void readMp3BitrateAndRate(const char* path, int& outBitrateKbps, int& outSampleRateKHz)
{
    outBitrateKbps = 0;  // 0 = unknown / not playing
    outSampleRateKHz = 0;

    FILE* f = fopen(path, "rb");
    if (!f) return;

    unsigned char buf[10];
    if (fread(buf, 1, 10, f) != 10) { fclose(f); return; }

    // skip ID3 tag if present
    if (buf[0]=='I' && buf[1]=='D' && buf[2]=='3') {
        int tagSize = (buf[6]<<21)|(buf[7]<<14)|(buf[8]<<7)|buf[9];
        fseek(f, tagSize, SEEK_CUR);
        fread(buf, 1, 4, f);
    }

    if ((buf[0]==0xFF) && ((buf[1]&0xE0)==0xE0))
    {
        int versionIndex = (buf[1] >> 3) & 0x03;
        int layerIndex   = (buf[1] >> 1) & 0x03;
        int brIndex      = (buf[2] >> 4) & 0x0F;
        int srIndex      = (buf[2] >> 2) & 0x03;

        static const int bitrates[2][3][16] = {
            { // MPEG-2
                {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0}, // Layer III
                {0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0}, // Layer II
                {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}   // Layer I
            },
            { // MPEG-1
                {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0},  // Layer III
                {0,32,48,56,64,80,96,112,128,160,192,224,256,320,0},      // Layer II
                {0,32,64,80,96,112,128,160,192,224,256,320,384,0,0,0}     // Layer I
            }
        };

        static const int sampleRates[2][4] = {
            {22050,24000,16000,0}, // MPEG-2
            {44100,48000,32000,0}  // MPEG-1
        };

        int version = (versionIndex==3)?1:0; // 1=MPEG1, 0=MPEG2
        int layer   = 3 - layerIndex;        // Layer I=3, II=2, III=1

        outBitrateKbps   = bitrates[version][layer-1][brIndex];
        outSampleRateKHz = sampleRates[version][srIndex]/1000;
    }

    fclose(f);
}

// --- Get MP3 duration using bitrate (works for CBR, approximate for VBR) ---
static int getMp3DurationSeconds(const char* path, int bitrateKbps)
{
    if (bitrateKbps <= 0) return 0;

    struct stat st;
    if (stat(path, &st) != 0) return 0;

    double seconds = (double)st.st_size * 8 / (bitrateKbps * 1000);
    return (int)seconds;
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

    // read bitrate & sample rate (zero if unknown)
    readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz);

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
        readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz);
        entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

        playlistMetadata.push_back(entry);
    }
}

const Mp3MetadataEntry* mp3GetTrackMetadata(int index)
{
    if (index < 0 || index >= (int)playlistMetadata.size()) return nullptr;
    return &playlistMetadata[index];
}

int mp3GetPlaylistCount()
{
    return playlistMetadata.size();
}

bool mp3Load(const char* path)
{
    if (!path) return false;

    playlistAdd(path);

    Mp3MetadataEntry entry;
    readMp3Metadata(path, entry);
    readMp3BitrateAndRate(path, entry.bitrateKbps, entry.sampleRateKHz);
    entry.durationSeconds = getMp3DurationSeconds(path, entry.bitrateKbps);

    playlistMetadata.push_back(entry);
    return true;
}
