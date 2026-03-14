#pragma once
#include <cmath>

class Biquad
{
public:
    void setupPeaking(float sampleRate,
                      float frequency,
                      float q,
                      float gainDB);

    float process(float in);

    float getMagnitude(float freq, float sampleRate) const;
    
private:
    float a0 = 1, a1 = 0, a2 = 0;
    float b0 = 1, b1 = 0, b2 = 0;

    float z1 = 0, z2 = 0;
};
