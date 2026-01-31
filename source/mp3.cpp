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

// Estimate MP3 duration from file size & bitrate
static int getMp3DurationSeconds(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    unsigned char buf[10];
    if (fread(buf, 1, 10, f) != 10) { fclose(f); return 0; }

    // skip ID3 tag if present
    if (buf[0]=='I' && buf[1]=='D' && buf[2]=='3') {
        int tagSize = (buf[6]<<21)|(buf[7]<<14)|(buf[8]<<7)|buf[9];
        fseek(f, tagSize, SEEK_CUR);
        fread(buf, 1, 4, f); // first frame header
    }

    unsigned int bitrate = 128000; // default 128 kbps
    if ((buf[0]==0xFF) && ((buf[1]&0xE0)==0xE0))
    {
        int brIndex = (buf[2]>>4) & 0x0F;
        int bitrates[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
        bitrate = bitrates[brIndex]*1000;
    }

    fclose(f);

    struct stat st;
    if (stat(path, &st) != 0) return 0;

    double seconds = (double)st.st_size * 8 / bitrate;
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

    // Estimate duration
    entry.durationSeconds = getMp3DurationSeconds(path);

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
        entry.durationSeconds = getMp3DurationSeconds(path);
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
    entry.durationSeconds = getMp3DurationSeconds(path);

    playlistMetadata.push_back(entry);
    return true;
}
