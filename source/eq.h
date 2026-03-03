#pragma once
#include <array>

constexpr int EQ_BAND_COUNT = 11;



class Equalizer
{
public:
    void setBand(int index, float value);
    float getBand(int index) const;

    void setEnabled(bool enabled);
    bool isEnabled() const;
    void toggle();
    void reset();

    float getPreampLinear() const;


private:
    std::array<float, EQ_BAND_COUNT> bands{};  // -12.0f to +12.0f dB
    bool enabled = false;
    float preampLinear = 1.0f;
    void updatePreamp();
};

extern Equalizer g_equalizer;
