#pragma once
#include <array>
#include "biquad.h"

constexpr int EQ_BAND_COUNT = 11;
extern bool autoEQEnabled;
class Equalizer
{
public:
    void setPreamp(float db);
    float getPreamp() const;
    void setReplayGainPreamp(float db);
    void setReplayGain(float db, float peak, bool isAlbum);
//    void setReplayGain(float db, float peak);
    void setReplayGainDb(float db);
    float getReplayGainPreamp() const;
    void setBand(int index, float value);
    float getBand(int index) const;
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void toggle();

    void reset();

    float getPreampLinear() const;
    float processSample(float sample, int channel);
    const Biquad& getFilter(int index) const { return filtersL[index]; }
    void setSampleRate(float sr);

private:
    std::array<float, EQ_BAND_COUNT> bands{};
    bool enabled = false;
    float preampDb = 0.0f;
    float preampLinear = 1.0f;
    float sampleRate = 48000.0f;

    float q = 1.0f;

    Biquad filtersL[10];
    Biquad filtersR[10];

    void updatePreamp();
    void updateBandFilter(int index);
};
void updateAutoEQ();
extern Equalizer g_equalizer;
