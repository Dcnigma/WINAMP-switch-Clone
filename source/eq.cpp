#include <switch.h>
#include "eq.h"
#include "ui.h"
#include "settings.h"
#include "settings_state.h"
#include <algorithm>
#include <cmath>

Equalizer g_equalizer;

static float autoEQBuffer[1024];
static int autoEQIndex = 0;
static float replayGain = 1.0f;
static float replayGainDb = 0.0f;

static float loudnessAvg = 0.0f;
static const float TARGET_LOUDNESS = 0.15f; // tweak later
static float g_replayGainLinear = 1.0f;
#define SPECTRUM_BARS 20
extern float bandValues[SPECTRUM_BARS];
static float limiterThreshold = 0.90f;  // start limiting near full scale
static float limiterSoftness  = 4.0f;   // higher = softer curve
static float g_replayGainPreampDb = 0.0f;
static float g_replayGainPreampLinear = 1.0f;

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

void updateReplayGain(float sample)
{
    float absSample = std::fabs(sample);

    // smooth RMS-ish estimate
    loudnessAvg = loudnessAvg * 0.999f + absSample * 0.001f;

    if (loudnessAvg > 0.0001f)
    {
        float desiredGain = TARGET_LOUDNESS / loudnessAvg;

        // smooth gain changes (important!)
        replayGain = replayGain * 0.995f + desiredGain * 0.005f;

        // clamp (avoid insane boosts)
        replayGain = std::clamp(replayGain, 0.2f, 3.0f);

        replayGainDb = 20.0f * log10f(replayGain);
    }
}

static inline float softLimiter(float x)
{
    float absx = fabsf(x);

    if (absx <= limiterThreshold)
        return x;

    // soft knee compression
    float excess = absx - limiterThreshold;

    float compressed =
        limiterThreshold +
        (1.0f - limiterThreshold) *
        (1.0f - expf(-excess * limiterSoftness));

    return (x > 0.0f) ? compressed : -compressed;
}

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

void Equalizer::setReplayGainPreamp(float db)
{
    g_replayGainPreampDb = std::clamp(db, -12.0f, 12.0f);
    g_replayGainPreampLinear =
        std::pow(10.0f, g_replayGainPreampDb / 20.0f);
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

void Equalizer::setReplayGain(float db, float peak)
{
    float linear = std::pow(10.0f, db / 20.0f);

    // Prevent clipping using peak
    if (peak > 0.0f)
    {
        float safe = 1.0f / peak;
        if (linear > safe)
            linear = safe;
    }

    g_replayGainLinear = linear;
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
    // -------------------------
    // ReplayGain stage (ALWAYS active if enabled)
    // -------------------------
    float rg = g_settings.replayGainEnabled
        ? (g_replayGainLinear * g_replayGainPreampLinear)
        : 1.0f;

    sample *= rg;

    // -------------------------
    // If EQ disabled → skip filters but KEEP ReplayGain
    // -------------------------
    if (!enabled)
        return sample;

    // -------------------------
    // EQ preamp (separate from ReplayGain)
    // -------------------------
    sample *= (preampLinear * 0.85f); // keep your headroom

    // -------------------------
    // Filters
    // -------------------------
    for (int i = 0; i < 10; ++i)
    {
        if (bands[i + 1] == 0.0f)
            continue;

        if (channel == 0)
            sample = filtersL[i].process(sample);
        else
            sample = filtersR[i].process(sample);
    }

    // -------------------------
    // Soft clip + limiter
    // -------------------------
    sample = std::tanh(sample);
    sample = softLimiter(sample);

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

float Equalizer::getReplayGainPreamp() const
{
    return g_replayGainPreampDb;
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
