#include "eq.h"
#include <algorithm>
#include <cmath>

Equalizer g_equalizer;

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

    // Rebuild all filters with new rate
    for (int i = 1; i <= 10; ++i)
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

    filtersL[biquadIndex].setupPeaking(
        sampleRate,
        bandFrequencies[biquadIndex],
        q,
        gain
    );

    filtersR[biquadIndex].setupPeaking(
        sampleRate,
        bandFrequencies[biquadIndex],
        q,
        gain
    );
}

float Equalizer::getPreampLinear() const
{
    if (!enabled)
        return 1.0f;

    return preampLinear;
}


void Equalizer::setBand(int index, float value)
{
    if (index < 0 || index >= EQ_BAND_COUNT)
        return;

    bands[index] = std::clamp(value, -12.0f, 12.0f);

    updateBandFilter(index);

}

float Equalizer::processSample(float sample, int channel)
{
    if (!enabled)
        return sample;

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
    for (auto& b : bands)
        b = 0.0f;
}
