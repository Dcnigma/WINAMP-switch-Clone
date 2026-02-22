#include "audio_engine.h"

bool AudioEngine::init(int sampleRate, int ch) {
    channels = ch;

    SDL_AudioSpec want{};
    want.freq = sampleRate;
    want.format = AUDIO_F32SYS;
    want.channels = static_cast<Uint8>(channels);
    want.samples = 2048; // safer on Switch
    want.callback = AudioEngine::audioCallback;
    want.userdata = this;

    SDL_AudioSpec have{};
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device) return false;

    return true;
}

void AudioEngine::start() {
    if (device) SDL_PauseAudioDevice(device, 0);
}

void AudioEngine::stop() {
    if (device) SDL_PauseAudioDevice(device, 1);
}

void AudioEngine::shutdown() {
    if (device) {
        SDL_CloseAudioDevice(device);
        device = 0;
    }
}

void AudioEngine::setPaused(bool p) {
    paused.store(p);
}

size_t AudioEngine::availableRead() const
{
    size_t r = readPos.load(std::memory_order_acquire);
    size_t w = writePos.load(std::memory_order_acquire);
    return w - r;
}

size_t AudioEngine::availableWrite() const
{
    size_t r = readPos.load(std::memory_order_acquire);
    size_t w = writePos.load(std::memory_order_acquire);
    return BUFFER_SIZE - (w - r);
}

void AudioEngine::pushPCM(const float* data, size_t samples)
{
    size_t r = readPos.load(std::memory_order_acquire);
    size_t w = writePos.load(std::memory_order_relaxed);

    size_t freeSpace = BUFFER_SIZE - (w - r);
    if (samples > freeSpace)
        samples = freeSpace; // safely drop excess

    for (size_t i = 0; i < samples; ++i)
        buffer[(w + i) % BUFFER_SIZE] = data[i];

    writePos.store(w + samples, std::memory_order_release);
}

void AudioEngine::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* engine = static_cast<AudioEngine*>(userdata);
    float* out = reinterpret_cast<float*>(stream);

    const size_t samplesRequested = len / sizeof(float);
    size_t samplesWritten = 0;

    if (engine->paused.load(std::memory_order_acquire)) {
        std::memset(stream, 0, len);
        return;
    }

    while (samplesWritten < samplesRequested) {
        size_t r = engine->readPos.load(std::memory_order_relaxed);
        size_t w = engine->writePos.load(std::memory_order_acquire);

        size_t available = w - r;
        if (available == 0) break;

        size_t toCopy = std::min(available,
                                 samplesRequested - samplesWritten);

        for (size_t i = 0; i < toCopy; ++i) {
            out[samplesWritten + i] =
                engine->buffer[(r + i) % BUFFER_SIZE];
        }

        engine->readPos.store(r + toCopy, std::memory_order_release);
        samplesWritten += toCopy;
    }

    // Always fill remainder with silence
    if (samplesWritten < samplesRequested) {
        std::memset(out + samplesWritten, 0,
                    (samplesRequested - samplesWritten) * sizeof(float));
    }
}
