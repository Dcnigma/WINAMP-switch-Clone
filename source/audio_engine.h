#pragma once
#include <SDL.h>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <algorithm>


// bool audioEngineInit(int sampleRate, int channels);
// void audioEngineShutdown();

void audioEnginePush(const int16_t* data, size_t samples);
size_t audioEngineAvailable();

bool audioEngineIsStarved();

class AudioEngine {
public:
    bool init(int sampleRate, int channels);
    void shutdown();

    void start();
    void stop();

    void pushPCM(const float* data, size_t samples);
    void setPaused(bool p);

    size_t availableRead() const;
    size_t availableWrite() const;

private:
    static void audioCallback(void* userdata, Uint8* stream, int len);

    static constexpr size_t BUFFER_SIZE = 44100 * 2; // ~1 sec stereo
    float buffer[BUFFER_SIZE]{};

    std::atomic<size_t> readPos{0};
    std::atomic<size_t> writePos{0};
    std::atomic<bool> paused{false};

    SDL_AudioDeviceID device = 0;
    int channels = 2;
};
