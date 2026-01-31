#include "mp3.h"
#include "playlist.h"
#include <vector>
#include <stdio.h>
#include <string.h>

static std::vector<Mp3MetadataEntry> playlistMetadata;

/* ---------- Helpers ---------- */

static unsigned int syncsafeToInt(unsigned char* b)
{
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3];
}

static void readTextFrame(FILE* f, unsigned int size, char* out, size_t outSize)
{
    if (size < 1) return;

    unsigned char encoding;
    fread(&encoding, 1, 1, f);
    size--;

    size_t toRead = (size < outSize - 1) ? size : outSize - 1;
    fread(out, 1, toRead, f);
    out[toRead] = 0;

    if (size > toRead)
        fseek(f, size - toRead, SEEK_CUR);
}

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
    playlistScroll = 0;
}
bool mp3Load(const char* path)
{
    if (!path) return false;

    playlistAdd(path);

    Mp3MetadataEntry entry;
    readMp3Metadata(path, entry);
    playlistMetadata.push_back(entry);

    return true;
}
