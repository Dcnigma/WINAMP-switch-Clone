#pragma once
#include <stdbool.h>

typedef enum {
    REPEAT_OFF,
    REPEAT_ALL,
    REPEAT_ONE
} RepeatMode;

typedef struct {
    int trackIndex;
    int elapsedSeconds;
    int durationSeconds;
    int sampleRate;
    int channels;
    bool isPlaying;
    bool isDecoding;

    // 🔵 NEW
    bool isPaused;
    bool shuffle;
    RepeatMode repeat;
} PlayerState;
