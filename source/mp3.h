#ifndef MP3_H
#define MP3_H
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <switch.h>      // gives socketInitializeDefault + nxlinkStdio

#define MP3_CACHE_VERSION 2

// Folder tracking
bool mp3IsFolderLoaded(const char* path);
void mp3SetLoadedFolder(const char* path);
void mp3CancelAllScans();

// Optional debug logging
void debugLog(const char* fmt, ...);

struct Mp3CacheHeader {
    uint32_t magic;   // 'MP3C'
    uint32_t version;
};

struct Mp3MetadataEntry
{
    char title[128];
    char artist[128];
    int durationSeconds;
    int channels;
    int bitrateKbps;
    int sampleRateKHz;
    int id3TagBytes;
};

struct RuntimeMetadata
{
    char path[512];
    Mp3MetadataEntry meta;
};

struct Mp3CacheEntry
{
    char path[512];
    time_t mtime;
    Mp3MetadataEntry meta;
};

// Cache

void mp3LoadCache(const char* folderKey);
void mp3StartBackgroundScanner();
void mp3StopBackgroundScanner();

// --- Playlist metadata management ---
void mp3LoadPlaylist();

const Mp3MetadataEntry* mp3GetTrackMetadata(int index);
int mp3GetPlaylistCount();

// --- Load single MP3 ---
bool mp3Load(const char* path);               // load a single MP3, add to playlist & metadata
bool mp3AddToPlaylist(const char* path);
void mp3ReloadAllMetadata();
void mp3ClearMetadata();

#endif // MP3_H
