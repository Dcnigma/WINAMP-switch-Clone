#pragma once

#include "mp3.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* -------------------------------------------------------
   WAV decoder handle
   Parses the RIFF/WAV header and streams raw PCM data,
   converting 8/16/24/32-bit mono/stereo to int16_t stereo.
   No library needed — WAV is uncompressed raw PCM.
------------------------------------------------------- */
struct WavDecoder
{
    FILE*    file         = nullptr;
    uint32_t sampleRate   = 0;
    uint32_t channels     = 0;
    uint32_t bitsPerSample = 0;
    uint32_t audioFormat  = 0;  // 1 = PCM, 3 = IEEE float
    uint64_t totalSamples = 0;  // PCM frames
    uint64_t samplesRead  = 0;
    long     dataOffset   = 0;  // file offset to first audio byte
    uint32_t dataBytes    = 0;  // total bytes in data chunk
    bool     eof          = false;
};

/* -------------------------------------------------------
   Lifecycle
------------------------------------------------------- */
WavDecoder* wavOpen(const char* path);
void        wavClose(WavDecoder* wd);

/* -------------------------------------------------------
   Decoding
------------------------------------------------------- */
enum WavReadResult { WAV_READ_OK, WAV_READ_DONE, WAV_READ_ERR };

WavReadResult wavRead(WavDecoder* wd,
                      unsigned char* buffer,
                      size_t         bufBytes,
                      size_t*        bytesRead);

/* -------------------------------------------------------
   Seeking
------------------------------------------------------- */
bool wavSeek(WavDecoder* wd, uint64_t targetSample);

/* -------------------------------------------------------
   Metadata / playlist API  (mirrors mp3.h / flac.h / ogg.h)
------------------------------------------------------- */
bool wavIsFolderLoaded(const char* path);
void wavSetLoadedFolder(const char* path);
void wavCancelAllScans();

void wavStartBackgroundScanner();
void wavStopBackgroundScanner();

bool wavAddToPlaylist(const char* path);
void wavClearMetadata();
void wavLoadCache(const char* folderKey);

const Mp3MetadataEntry* wavGetTrackMetadata(int globalIndex);
int                     wavGetPlaylistCount();
