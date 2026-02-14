#pragma once
#include <stdbool.h>

struct PlayerState
{
    int trackIndex;          // -1 = stopped
    int elapsedSeconds;
    int durationSeconds;

    int sampleRate;
    int channels;

    bool isPlaying;          // audible output
    bool isDecoding;         // mpg123 still feeding data
};

const PlayerState* playerGetState();
