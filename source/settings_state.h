#pragma once

enum SettingsItems
{
    SETTING_CROSSFADE,
    SETTING_CROSSFADE_TIME,
    SETTING_REPLAYGAIN,
    SETTING_AUTOGAIN,
    SETTING_BACK,
    SETTINGS_COUNT
};

enum ReplayGainMode
{
    REPLAYGAIN_OFF = 0,
    REPLAYGAIN_TRACK,
    REPLAYGAIN_ALBUM
};

struct PlayerSettings
{
    bool crossfadeEnabled;
    float crossfadeSeconds;
    bool autoGainEnabled;
    ReplayGainMode replayGainMode;
};


extern PlayerSettings g_settings;
