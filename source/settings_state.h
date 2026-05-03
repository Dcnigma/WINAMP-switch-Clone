#pragma once

enum ReplayGainMode
{
    REPLAYGAIN_OFF = 0,
    REPLAYGAIN_TRACK,
    REPLAYGAIN_ALBUM
};

// Page 1 Settings (existing)
enum Setting
{
    SETTING_CROSSFADE = 0,
    SETTING_CROSSFADE_TIME,
    SETTING_REPLAYGAIN,
    SETTING_AUTOGAIN,
    
    // Page 2 Settings (new!)
    SETTING_TOUCH_SENSITIVITY,
    SETTING_TOUCH_SPEED_LIMIT,
    SETTING_STAY_AWAKE,
    
    // Navigation/Actions
    SETTING_SAVESETTINGS,
    SETTING_BACK,
    
    SETTINGS_COUNT  // Must be last
};

struct PlayerSettings
{
    // Page 1
    bool crossfadeEnabled;
    float crossfadeSeconds;       // 0.5 - 10.0
    bool autoGainEnabled;
    ReplayGainMode replayGainMode;
    
    // Page 2
    float touchSensitivity;       // 20.0 - 80.0 pixels per song
    int touchSpeedLimit;          // 1 - 10 swaps per frame
    bool stayAwakeEnabled;        // Keep screen awake during playback
};

extern PlayerSettings g_settings;
