#include "eq.h"
#include <algorithm>
#include <cmath>

Equalizer g_equalizer;

static float autoEQBuffer[1024];
static int autoEQIndex = 0;
//bool autoEQEnabled = false;

static const float bandFrequencies[10] =
{
    60.0f,
    170.0f,
    310.0f,
    600.0f,
    1000.0f,
    3000.0f,
    6000.0f,
    12000.0f,
    14000.0f,
    16000.0f
};

void Equalizer::setSampleRate(float sr)
{
    sampleRate = sr;

    for (int i = 1; i < 10; ++i)
        updateBandFilter(i);
}

void Equalizer::setPreamp(float db)
{
    db = std::clamp(db, -12.0f, 12.0f);

    preampDb = db;
    preampLinear = std::pow(10.0f, db / 20.0f);
}

// void Equalizer::updateBandFilter(int index)
// {
//     if (index < 0 || index >= EQ_BAND_COUNT)
//         return;
//
//     float gain = bands[index];
//     float freq = bandFrequencies[index];
//
//     float nyquist = sampleRate * 0.5f;
//
//     if (freq >= nyquist)
//         freq = nyquist * 0.9f;
//
//     filtersL[index].setupPeaking(sampleRate, freq, q, gain);
//     filtersR[index].setupPeaking(sampleRate, freq, q, gain);
// }

void Equalizer::updateBandFilter(int index)
{
    if (index < 1 || index > 10)
        return;

    float gain = bands[index];
    int biquadIndex = index - 1;

    float freq = bandFrequencies[biquadIndex];

    float nyquist = sampleRate * 0.5f;
    if (freq > nyquist * 0.9f)
        freq = nyquist * 0.9f;

    filtersL[biquadIndex].setupPeaking(sampleRate, freq, q, gain);
    filtersR[biquadIndex].setupPeaking(sampleRate, freq, q, gain);
}

float Equalizer::getPreampLinear() const
{
    if (!enabled)
        return 1.0f;

    return preampLinear;
}


void Equalizer::setBand(int index, float value)
{
    if (index < 1 || index > 10)
        return;

    bands[index] = std::clamp(value, -12.0f, 12.0f);

    updateBandFilter(index);
}

void updateAutoEQ()
{
    if (!autoEQEnabled)
        return;

    const int N = 1024;

    float bandEnergy[10] = {0};

    for (int i = 0; i < N; i++)
    {
        float s = autoEQBuffer[i];
        float power = s * s;

        // crude frequency approximation
        int band = (i * 10) / N;
        if (band >= 10) band = 9;

        bandEnergy[band] += power;
    }

    static float smoothed[10] = {0};

    for (int b = 0; b < 10; b++)
    {
        smoothed[b] = smoothed[b] * 0.8f + bandEnergy[b] * 0.2f;

        float db = log10f(smoothed[b] + 1e-6f) * 6.0f;

        if (db > 6) db = 6;
        if (db < -6) db = -6;

        g_equalizer.setBand(b + 1, db);
    }
}

float Equalizer::processSample(float sample, int channel)
{
    if (!enabled)
        return sample;

    autoEQBuffer[autoEQIndex++] = sample;

    if (autoEQIndex >= 1024)
        autoEQIndex = 0;

    // Apply preamp
    sample *= preampLinear;

    for (int i = 0; i < 10; ++i)
    {
        if (bands[i + 1] == 0.0f)
            continue;

        if (channel == 0)
            sample = filtersL[i].process(sample);
        else
            sample = filtersR[i].process(sample);
    }

    return sample;
}

float Equalizer::getBand(int index) const
{
    if (index < 0 || index >= EQ_BAND_COUNT)
        return 0.0f;

    return bands[index];
}

void Equalizer::toggle()
{
    enabled = !enabled;

}

float Equalizer::getPreamp() const
{
    return preampDb;
}

void Equalizer::setEnabled(bool state)
{
    enabled = state;
}

bool Equalizer::isEnabled() const
{
    return enabled;
}

void Equalizer::reset()
{
    for (int i = 0; i < 10; i++)
    {
        bands[i] = 0.0f;
        updateBandFilter(i);
    }
}
