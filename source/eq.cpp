#include "eq.h"
#include <algorithm>
#include <cmath>

Equalizer g_equalizer;

float Equalizer::getPreampLinear() const
{
    if (!enabled)
        return 1.0f;

    return preampLinear;
}

void Equalizer::updatePreamp()
{
    float db = bands[0];
    preampLinear = std::pow(10.0f, db / 20.0f);
}

void Equalizer::setBand(int index, float value)
{
    if (index < 0 || index >= EQ_BAND_COUNT)
        return;

    bands[index] = std::clamp(value, -12.0f, 12.0f);

    if (index == 0)
        updatePreamp();
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
