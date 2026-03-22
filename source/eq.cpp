#include <switch.h>
#include "eq.h"
#include "ui.h"
#include <algorithm>
#include <cmath>

Equalizer g_equalizer;

static float autoEQBuffer[1024];
static int autoEQIndex = 0;
//bool autoEQEnabled = false;
//extern float bandValues[20];
//extern const int SPECTRUM_BARS;

#define SPECTRUM_BARS 20
extern float bandValues[SPECTRUM_BARS];

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

// static void simpleFFT(const float* inReal, float* outMag, int N)
// {
//     for (int k = 0; k < N / 2; k++)
//     {
//         float real = 0.0f;
//         float imag = 0.0f;
//
//         for (int n = 0; n < N; n++)
//         {
//             float phase = 2.0f * M_PI * k * n / N;
//             real += inReal[n] * cosf(phase);
//             imag -= inReal[n] * sinf(phase);
//         }
//
//         outMag[k] = sqrtf(real * real + imag * imag);
//     }
// }

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

    const int EQ_BANDS = 10;
    const int BARS_PER_BAND = SPECTRUM_BARS / EQ_BANDS;

    static float smoothed[EQ_BANDS] = {0};

    for (int b = 0; b < EQ_BANDS; b++)
    {
        float energy = 0.0f;

        // merge spectrum bars into EQ bands
        for (int i = 0; i < BARS_PER_BAND; i++)
        {
            int idx = b * BARS_PER_BAND + i;
            energy += bandValues[idx];
        }

        energy /= BARS_PER_BAND;

        smoothed[b] = smoothed[b] * 0.97f + energy * 0.03f;

        // invert behavior (flatten instead of boost)
        float db = (0.5f - smoothed[b]);

        // nonlinear response
        db = db * std::abs(db) * 18.0f;

        // prefer cuts
        if (db > 0)
            db *= 0.6f;

        // clamp
        db = std::clamp(db, -6.0f, 6.0f);

        // smooth band movement
        float current = g_equalizer.getBand(b + 1);
        float diff = db - current;

        diff = std::clamp(diff, -0.3f, 0.3f);

        g_equalizer.setBand(b + 1, current + diff);
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
