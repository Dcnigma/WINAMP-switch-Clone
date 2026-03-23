#pragma once

enum
{
    SETTING_CROSSFADE,
    SETTING_CROSSFADE_TIME,
    SETTING_REPLAYGAIN,
    SETTING_REPLAYGAIN_PREAMP,
    SETTING_AUTOEQ,
    SETTING_BACK,
    SETTINGS_COUNT
};

struct PlayerSettings
{
    bool crossfadeEnabled;
    float crossfadeSeconds;
    bool replayGainEnabled;
};


extern PlayerSettings g_settings;
