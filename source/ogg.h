#pragma once

#include "mp3.h"
#include <stdint.h>
#include <stdbool.h>
#include <vorbis/vorbisfile.h>

/* -------------------------------------------------------
   OGG/Vorbis decoder handle
   Wraps libvorbisfile and outputs int16_t stereo PCM,
   matching the interface used by the FLAC and MP3 decoders.
------------------------------------------------------- */
struct OggDecoder
{
    OggVorbis_File vf;
    bool           open        = false;
    uint32_t       sampleRate  = 0;
    uint32_t       channels    = 0;
    int64_t        totalSamples = 0; // PCM frames in file (-1 if unknown)
    int64_t        samplesRead  = 0;
    bool           eof         = false;
};

/* -------------------------------------------------------
   Lifecycle
------------------------------------------------------- */
OggDecoder* oggOpen(const char* path);
void        oggClose(OggDecoder* od);

/* -------------------------------------------------------
   Decoding  —  same result enum pattern as flac.h
------------------------------------------------------- */
enum OggReadResult { OGG_READ_OK, OGG_READ_DONE, OGG_READ_ERR };

OggReadResult oggRead(OggDecoder* od,
                      unsigned char* buffer,
                      size_t         bufBytes,
                      size_t*        bytesRead);

/* -------------------------------------------------------
   Seeking
------------------------------------------------------- */
bool oggSeek(OggDecoder* od, uint64_t targetSample);

/* -------------------------------------------------------
   Metadata / playlist API  (mirrors mp3.h / flac.h)
------------------------------------------------------- */
bool oggIsFolderLoaded(const char* path);
void oggSetLoadedFolder(const char* path);
void oggCancelAllScans();

void oggStartBackgroundScanner();
void oggStopBackgroundScanner();

bool oggAddToPlaylist(const char* path);
void oggClearMetadata();
void oggLoadCache(const char* folderKey);

const Mp3MetadataEntry* oggGetTrackMetadata(int globalIndex);
int                     oggGetPlaylistCount();
