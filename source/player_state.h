#pragma once

enum RepeatMode {
    REPEAT_OFF = 0,
    REPEAT_ALL,
    REPEAT_ONE
};

struct PlayerState {
    int trackIndex;
    int elapsedSeconds;
    int durationSeconds;
    int sampleRate;
    int channels;

    bool playing;
    bool paused;

    bool shuffle;
    RepeatMode repeat;
};

extern PlayerState g_state;
