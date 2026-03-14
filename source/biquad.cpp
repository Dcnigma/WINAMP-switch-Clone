#include "biquad.h"

void Biquad::setupPeaking(float sampleRate,
                          float frequency,
                          float q,
                          float gainDB)
{
    float A  = std::pow(10.0f, gainDB / 40.0f);
    float w0 = 2.0f * M_PI * frequency / sampleRate;
    float alpha = std::sin(w0) / (2.0f * q);
    float cosw0 = std::cos(w0);

    float b0_ = 1 + alpha * A;
    float b1_ = -2 * cosw0;
    float b2_ = 1 - alpha * A;
    float a0_ = 1 + alpha / A;
    float a1_ = -2 * cosw0;
    float a2_ = 1 - alpha / A;

    b0 = b0_ / a0_;
    b1 = b1_ / a0_;
    b2 = b2_ / a0_;
    a1 = a1_ / a0_;
    a2 = a2_ / a0_;
}

float Biquad::process(float in)
{
    float out = b0 * in + z1;
    z1 = b1 * in - a1 * out + z2;
    z2 = b2 * in - a2 * out;
    return out;
}

float Biquad::getMagnitude(float freq, float sampleRate) const
{
    float w = 2.0f * M_PI * freq / sampleRate;

    float cosw = cosf(w);
    float sinw = sinf(w);

    float cos2w = cosf(2 * w);
    float sin2w = sinf(2 * w);

    float numReal = b0 + b1 * cosw + b2 * cos2w;
    float numImag = b1 * sinw + b2 * sin2w;

    float denReal = 1 + a1 * cosw + a2 * cos2w;
    float denImag = a1 * sinw + a2 * sin2w;

    float numMag = sqrtf(numReal * numReal + numImag * numImag);
    float denMag = sqrtf(denReal * denReal + denImag * denImag);

    return numMag / denMag;
}
