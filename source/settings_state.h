#pragma once

enum
{
    SETTING_CROSSFADE,
    SETTING_CROSSFADE_TIME,
    SETTING_AUTO_EQ,
    SETTING_BACK,

    SETTINGS_COUNT
};

struct PlayerSettings
{
    bool crossfadeEnabled;
    float crossfadeSeconds;
};


extern PlayerSettings g_settings;
