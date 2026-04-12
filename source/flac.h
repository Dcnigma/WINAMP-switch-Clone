#pragma once

#include "mp3.h"      // reuse Mp3MetadataEntry — same fields, works for FLAC too
#include <stdint.h>
#include <stdbool.h>
#include <FLAC/stream_decoder.h>

/* -------------------------------------------------------
   FLAC decoder handle
   Wraps a libFLAC stream decoder and exposes PCM in the
   same int16_t stereo format player.cpp expects.
------------------------------------------------------- */
struct FlacDecoder
{
    FLAC__StreamDecoder* decoder = nullptr;
    FILE*                file    = nullptr;

    // Format info (filled after first metadata callback)
    uint32_t sampleRate   = 0;
    uint32_t channels     = 0;
    uint32_t bitsPerSample = 0;
    uint64_t totalSamples = 0;   // total PCM frames in file
    uint64_t samplesRead  = 0;   // frames decoded so far

    // Decoded PCM ring-buffer (int16_t interleaved stereo)
    static constexpr int BUF_FRAMES = 8192;
    int16_t  pcmBuf[BUF_FRAMES * 2]; // stereo → *2
    int      pcmHead  = 0;  // read position
    int      pcmTail  = 0;  // write position
    int      pcmCount = 0;  // frames available
    bool     eof      = 0;
    bool     error    = false;
};

/* -------------------------------------------------------
   Lifecycle
------------------------------------------------------- */
// Open a FLAC file for decoding. Returns nullptr on failure.
FlacDecoder* flacOpen(const char* path);

// Close and free a decoder created with flacOpen.
void flacClose(FlacDecoder* fd);

/* -------------------------------------------------------
   Decoding
   Mirrors the mpg123_read() interface used in player.cpp:
     - fills `buffer` with up to `bufBytes` bytes of
       int16_t stereo PCM at the file's native sample rate.
     - sets *bytesRead to actual bytes written.
     - returns FLAC_READ_OK, FLAC_READ_DONE, or FLAC_READ_ERR.
------------------------------------------------------- */
enum FlacReadResult { FLAC_READ_OK, FLAC_READ_DONE, FLAC_READ_ERR };

FlacReadResult flacRead(FlacDecoder* fd,
                        unsigned char* buffer,
                        size_t bufBytes,
                        size_t* bytesRead);

/* -------------------------------------------------------
   Seeking
   targetSample is a PCM frame offset (like mpg123_seek).
   Returns true on success.
------------------------------------------------------- */
bool flacSeek(FlacDecoder* fd, uint64_t targetSample);

/* -------------------------------------------------------
   Metadata / playlist API  (mirrors mp3.h)
------------------------------------------------------- */
// Folder tracking (shared concept with mp3 layer)
bool flacIsFolderLoaded(const char* path);
void flacSetLoadedFolder(const char* path);
void flacCancelAllScans();

// Background scanner lifecycle
void flacStartBackgroundScanner();
void flacStopBackgroundScanner();

// Playlist
bool flacAddToPlaylist(const char* path);
void flacClearMetadata();
void flacLoadCache(const char* folderKey);

const Mp3MetadataEntry* flacGetTrackMetadata(int index);
int                     flacGetPlaylistCount();
