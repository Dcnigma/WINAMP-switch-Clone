#ifndef MP3_H
#define MP3_H
#pragma once

#include <stdint.h>
#include <stdbool.h>

struct Mp3MetadataEntry
{
    char title[128];
    char artist[128];
    int durationSeconds;
    int channels;
    int bitrateKbps;    // NEW: bitrate in kbps
    int sampleRateKHz;  // NEW: sample rate in kHz
};

// --- Playlist metadata management ---
void mp3LoadPlaylist();                        // build metadata for all files in playlist
const Mp3MetadataEntry* mp3GetTrackMetadata(int index);
int mp3GetPlaylistCount();

// --- Load single MP3 ---
bool mp3Load(const char* path);               // load a single MP3, add to playlist & metadata
bool mp3AddToPlaylist(const char* path);
void mp3ReloadAllMetadata();
void mp3ClearMetadata();

#endif // MP3_H
