#ifndef MP3_H
#define MP3_H
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MP3_TITLE_LEN 128
#define MP3_ARTIST_LEN 128

typedef struct {
    char title[MP3_TITLE_LEN];
    char artist[MP3_ARTIST_LEN];
    int durationSeconds; // optional, calculate later if needed
} Mp3MetadataEntry;

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
