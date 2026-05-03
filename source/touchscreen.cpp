#include "touchscreen.h"
#include "player.h"
#include "player_state.h"
#include "playlist.h"
#include "filebrowser.h"
#include "settings.h"
#include "settings_state.h"
#include "ui.h"
#include "eq.h"

#include <switch.h>
#include <stdio.h>
#include <math.h>

extern bool autoEQEnabled;
extern int  selectedBand;
extern PlayerSettings g_settings;

/* ============================================================
   COORDINATE MAPPING
   ============================================================
   The Switch touchscreen reports coordinates in the physical
   screen space: 1280 × 720 landscape (matching the LCD panel).

   Your framebuffer is also 1920 × 1080 landscape, drawn rotated
   90° CW so it appears portrait when the Switch is held upright.

   Step 1 — Scale touch coords from 1280×720 to 1920×1080:
     fbX = touchX * (1920.0 / 1280.0)   =  touchX * 1.5
     fbY = touchY * (1080.0 /  720.0)   =  touchY * 1.5

   Step 2 — The rects in ui.cpp are in framebuffer coordinates,
   so after scaling we can use them directly with SDL_PointInRect.
   No rotation is needed here because both the touch panel and
   the framebuffer are landscape; the visual rotation is purely
   a render transform and doesn't affect hit testing.

   If you find taps are consistently off, adjust TOUCH_SCALE_X/Y
   or the TOUCH_OFFSET values below.
============================================================ */
#define TOUCH_SCALE_X  1.5f   // 1920 / 1280
#define TOUCH_SCALE_Y  1.5f   // 1080 / 720
#define TOUCH_OFFSET_X 0      // fine-tune if needed
#define TOUCH_OFFSET_Y 0

/* ============================================================
   DRAG TRACKING
   A drag is a touch that moves without lifting.
   Used for volume bar, pan slider, progress bar, EQ bands,
   playlist slider, and playlist reordering.
============================================================ */
#define MAX_TOUCHES 10

struct TouchPoint {
    int   id;         // finger ID from HID
    float fbX, fbY;   // current position in framebuffer coords
    float startFbX, startFbY;
    bool  active;
    bool  moved;      // true once the finger has moved > DRAG_THRESHOLD px
    int   draggedPlaylistIndex; // for playlist reordering
};

static HidTouchScreenState g_touchState;
static TouchPoint           g_touches[MAX_TOUCHES] = {};
static int                  g_touchCount = 0;

// A tap is only registered if the finger moved less than this many px
#define TAP_THRESHOLD    20
// How far a drag must move before we stop treating it as a tap
#define DRAG_THRESHOLD   12

/* ============================================================
   HELPERS
============================================================ */
static bool rectContains(const SDL_Rect& r, float x, float y)
{
    return (x >= r.x && x < r.x + r.w &&
            y >= r.y && y < r.y + r.h);
}

// Convert raw HID touch position to framebuffer coordinates
static void toFB(int rawX, int rawY, float& outX, float& outY)
{
    outX = (float)rawX * TOUCH_SCALE_X + TOUCH_OFFSET_X;
    outY = (float)rawY * TOUCH_SCALE_Y + TOUCH_OFFSET_Y;
}

// Clamp a float to [lo, hi]
static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ============================================================
   SLIDER HIT HELPERS
   These mirror the slider geometry in ui.cpp exactly.
============================================================ */

// Volume bar: {1551, 421, 40, 264}
// Knob moves top→bottom for quiet→loud (volume increases downward)
static void handleVolumeBar(float fbY)
{
    const SDL_Rect bar = {1551, 421, 40, 264};
    float t = (fbY - bar.y) / (float)bar.h;
    t = clampf(t, 0.0f, 1.0f);
    playerSetVolume(t);
}

// Pan slider: {1552, 698, 40, 145}
// Knob moves top→bottom for left→right pan
static void handlePanSlider(float fbY)
{
    const SDL_Rect bar = {1552, 698, 40, 145};
    float t = (fbY - bar.y) / (float)bar.h;
    t = clampf(t, 0.0f, 1.0f);
    playerSetPan(t * 2.0f - 1.0f);   // map 0..1 → -1..+1
}

// Progress bar: {1467, 64, 61, 982}
// Indicator moves top→bottom for start→end of track
static void handleProgressBar(float fbY)
{
    if (!playerIsPlaying()) return;
    const SDL_Rect bar = {1467, 64, 61, 982};
    float t = (fbY - bar.y) / (float)bar.h;
    t = clampf(t, 0.0f, 1.0f);
    playerSeek(t * (float)playerGetTrackLength());
}

// Playlist slider: vertical scrollbar for playlist
// The knob position is calculated based on scroll position
static void handlePlaylistSlider(float fbX, float fbY)
{
    // Playlist slider coordinates from ui.cpp
    const int trackX = 208;
    const int trackY = 1020;
    const int trackW = 326;
    const int knobW = 103;
    const int knobH = 30;

    int totalTracks = playlistGetCount();
    if (totalTracks <= 4) return; // No scrolling needed if 4 or fewer tracks

    // The knob position is: t=1.0 → knob at right (scroll=0)
    //                       t=0.0 → knob at left (scroll=max)
    // So: t = 1.0 - (scroll / maxScroll)

    // Convert touch position to t value (0.0 to 1.0)
    int travel = trackW - knobW;
    float t = (fbX - trackX) / (float)travel;  // USER FIX: fbX instead of fbY
    t = clampf(t, 0.0f, 1.0f);

    // Reverse: t=1.0 means scroll=0, t=0.0 means scroll=max
    int maxScroll = totalTracks - 4;
    int newScroll = (int)((1.0f - t) * maxScroll + 0.5f);
    playlistSetScroll(newScroll);
}

// EQ band sliders — bands 1-10 are in eqBand2..eqBand11
// Each rect is {730, Y, 340, 53}; knob moves left→right for cut→boost
// Band value range is -12 .. +12 dB
// USER IMPROVEMENT: Increased height from 33 to 53 for easier touch
static const SDL_Rect g_eqBands[11] = {
    {730,  90, 340, 53},  // [0] = preamp
    {730, 315, 340, 53},  // [1] = band 1
    {730, 387, 340, 53},  // [2] = band 2
    {730, 457, 340, 53},  // [3] = band 3
    {730, 532, 340, 53},  // [4] = band 4
    {730, 603, 340, 53},  // [5] = band 5
    {730, 671, 340, 53},  // [6] = band 6
    {730, 743, 340, 53},  // [7] = band 7
    {730, 813, 340, 53},  // [8] = band 8
    {730, 885, 340, 53},  // [9] = band 9
    {730, 953, 340, 53},  // [10]= band 10
};

static void handleEQBand(int bandArrayIndex, float fbX)
{
    const SDL_Rect& bar = g_eqBands[bandArrayIndex];
    float t = (fbX - bar.x) / (float)bar.w;
    t = clampf(t, 0.0f, 1.0f);
    float db = t * 24.0f - 12.0f;   // 0..1 → -12..+12 dB

    if (bandArrayIndex == 0)
        g_equalizer.setPreamp(db);
    else
        g_equalizer.setBand(bandArrayIndex, db);
}



/* ============================================================
   PLAYLIST ROW HIT TESTING
   renderPlaylist draws 4 visible tracks in trackTitleArea region.
   From playlist.cpp: titleRect.x starts at 230, steps of 75.
   MAX_VISIBLE_TRACKS = 4, each column is 75px wide in FB-X.
============================================================ */
static int getPlaylistIndexAtPosition(float fbX, float fbY)
{
    // Each track column: x = 230 + (3-i)*75, width = 70
    // (reversed because drawn rotated)
    const int startX   = 230;
    const int colWidth = 75;
    const int visible  = 4;

    int scroll = playlistGetScroll();

    for (int i = 0; i < visible; i++)
    {
        int colX = startX + (visible - 1 - i) * colWidth;
        SDL_Rect col = {colX, 50, 70, 950};

        if (rectContains(col, fbX, fbY))
        {
            int trackIdx = scroll + i;
            if (trackIdx < playlistGetCount())
            {
                return trackIdx;
            }
        }
    }
    return -1;
}

static void handlePlaylistTap(float fbX, float fbY)
{
    int trackIdx = getPlaylistIndexAtPosition(fbX, fbY);
    if (trackIdx >= 0)
    {
        playlistSetCurrentIndex(trackIdx);
        playerPlay(trackIdx);
    }
}

/* ============================================================
   PLAYER BUTTON TAPS
   All rects taken directly from uiRender in ui.cpp
============================================================ */
static bool handlePlayerTap(float fbX, float fbY)
{
    // --- Playback buttons ---
    const SDL_Rect prevButton  = {1340,  60, 100,  90};
    const SDL_Rect playButton  = {1340, 151, 100,  90};
    const SDL_Rect pauseButton = {1340, 242, 100,  90};
    const SDL_Rect stopButton  = {1340, 332, 100,  90};
    const SDL_Rect nextButton  = {1340, 426, 100,  90};
    const SDL_Rect ejectButton = {1340, 532, 100,  90};

    if (rectContains(prevButton, fbX, fbY))
    {
        uiNotifyButtonPress(UI_BTN_PREV);
        if (playerGetPosition() > PREV_RESTART_THRESHOLD)
            playerSeek(0.0f);
        else
            playerPrev();
        return true;
    }
    if (rectContains(playButton, fbX, fbY))
    {
        uiNotifyButtonPress(UI_BTN_PLAY);
        if (playerIsPaused())
            playerTogglePause();
        else
            playerPlay(playlistGetCurrentIndex());
        return true;
    }
    if (rectContains(pauseButton, fbX, fbY))
    {
        uiNotifyButtonPress(UI_BTN_PAUSE);
        playerTogglePause();
        return true;
    }
    if (rectContains(stopButton, fbX, fbY))
    {
        uiNotifyButtonPress(UI_BTN_STOP);
        playerStop();
        return true;
    }
    if (rectContains(nextButton, fbX, fbY))
    {
        uiNotifyButtonPress(UI_BTN_NEXT);
        playerNext();
        return true;
    }
    if (rectContains(ejectButton, fbX, fbY))
    {
        // Eject = open file browser
        fileBrowserOpen();
        return true;
    }

    // --- Shuffle/Repeat buttons ---
    const SDL_Rect shuffleButton = {1357, 642, 72, 182};
    const SDL_Rect repeatButton  = {1357, 825, 72, 110};

    if (rectContains(shuffleButton, fbX, fbY))
    {
        playerToggleShuffle();
        return true;
    }
    if (rectContains(repeatButton, fbX, fbY))
    {
        playerCycleRepeat();
        return true;
    }

    // --- EQ on/off toggle ---
    const SDL_Rect eqPreset1 = {1112,  53,  73, 104};
    if (rectContains(eqPreset1, fbX, fbY))
    {
        g_equalizer.toggle();
        return true;
    }

    // --- Auto-EQ toggle ---
    const SDL_Rect eqPreset2 = {1112, 153,  73, 131};
    if (rectContains(eqPreset2, fbX, fbY))
    {
        autoEQEnabled = !autoEQEnabled;
        return true;
    }

    // --- Playlist buttons ---
    const SDL_Rect addPlaylist  = { 70,  42, 100, 100};
    const SDL_Rect rmPlaylist   = { 70, 158, 100, 100};

    if (rectContains(addPlaylist, fbX, fbY))
    {
        fileBrowserOpenadd();
        return true;
    }
    if (rectContains(rmPlaylist, fbX, fbY))
    {
        // Remove currently selected track (simple implementation)
        // Full multi-select removal can be wired up later
        return true;
    }

    // --- Settings open ---
    // No dedicated button in the skin, but tapping the kbps/kHz area opens settings
    const SDL_Rect TopLeftSign = {1845, 0, 75, 75};

    if (rectContains(TopLeftSign, fbX, fbY))
    {
        settingsOpen();
        return true;
    }

    // --- Playlist row tap ---
    handlePlaylistTap(fbX, fbY);

    return false;
}

/* ============================================================
   FILE BROWSER BUTTON TAPS
   Coordinates from filebrowser.cpp
============================================================ */
static bool handleFileBrowserTap(float fbX, float fbY)
{
    // File browser layout constants (from filebrowser.cpp)
    const int FBW = 1920;
    const int FBH = 1080;

    // Check which screen is active
    if (fileBrowserIsMenu())
    {
        // ========== MENU SCREEN ==========
        const int MENU_MARGIN_TOP = 800;
        const int MENU_TITLE_H = 80;
        const int MENU_ROW_H = 130;
        const int MENU_GAP = 12;

        int x = FBW - MENU_MARGIN_TOP;

        // Skip title (drawn last, appears at top)
        x += MENU_TITLE_H + MENU_GAP;

        // Rows are drawn in reverse order:
        // 1st drawn = "Add URL" (top of screen)
        // 2nd drawn = "Add FILES" (middle)
        // 3rd drawn = "A: SELECT B: CANCEL" hint (bottom)

        // "Add URL" row (menuSel=1, top-most button)
        x -= MENU_ROW_H;
        SDL_Rect addUrlRow = {x, 20, MENU_ROW_H, FBH - 40};
        x -= MENU_GAP;

        // "Add FILES" row (menuSel=0, middle button)
        x -= MENU_ROW_H;
        SDL_Rect addFilesRow = {x, 20, MENU_ROW_H, FBH - 40};
        x -= MENU_GAP;

        // Hint row (bottom-most, not selectable)
        x -= MENU_ROW_H;
        SDL_Rect hintRow = {x, 20, MENU_ROW_H, FBH - 40};

        // Check taps on rows
        if (rectContains(addFilesRow, fbX, fbY))
        {
            fileBrowserTapRow(-1);  // -1 = open browse screen
            return true;
        }
        if (rectContains(addUrlRow, fbX, fbY))
        {
            // "Add URL" - placeholder, do nothing for now
            return true;
        }

        // Tap outside menu rows (but inside menu area) = cancel
        // x is now below the hint row, so anything below x or above the title cancels
        if (fbX < x || fbX > FBW - MENU_MARGIN_TOP + MENU_TITLE_H)
        {
            fileBrowserCancel();
            return true;
        }

        return false;
    }
    else if (fileBrowserIsBrowse())
    {
        // ========== BROWSE SCREEN ==========
        const int BR_MARGIN_TOP = 400;
        const int BR_HDR_H = 80;
        const int BR_ROW_H = 140;
        const int BR_ROWS_MAX = 6;  // Maximum rows that can be displayed
        const int BR_BTNS_H = 80;
        const int BR_HINT_H = 60;
        const int BR_GAP = 110;
        const int BR_GAP2 = 10;

        // Get actual number of items to determine visible rows
        int itemCount = fileBrowserGetItemCount();
        int visibleRows = (itemCount < BR_ROWS_MAX) ? itemCount : BR_ROWS_MAX;

        int x = FBW - BR_MARGIN_TOP;

        // Header with scroll buttons
        x -= BR_HDR_H;

        // Scroll up button [<]
        const SDL_Rect scrollUpBtn = {x + (BR_HDR_H - 50)/2, FBH - 40 - 180, 50, 80};
        if (rectContains(scrollUpBtn, fbX, fbY))
        {
            fileBrowserScrollPage(-1, 0);
            return true;
        }

        // Scroll down button [>]
        const SDL_Rect scrollDownBtn = {x + (BR_HDR_H - 50)/2, FBH - 40 - 80, 50, 80};
        if (rectContains(scrollDownBtn, fbX, fbY))
        {
            fileBrowserScrollPage(+1, 0);
            return true;
        }

        x -= BR_GAP;
        x += BR_HDR_H;

        // File/folder rows - drawn bottom-to-top, use ACTUAL visible rows
        for (int vi = 0; vi < visibleRows; vi++)
        {
            x -= BR_ROW_H;
            SDL_Rect rowRect = {x, 0, BR_ROW_H, FBH};

            if (rectContains(rowRect, fbX, fbY))
            {
                // Reverse the index because rows are drawn from bottom to top
                int reversedIndex = visibleRows - 1 - vi;

                // Check if it's the add button (right side of row)
                const int ADD_BTN_W = 70;
                const int ADD_BTN_MARGIN = 10;
                SDL_Rect addBtnRect = {
                    x + (BR_ROW_H - 70)/2,
                    FBH - ADD_BTN_W - ADD_BTN_MARGIN,
                    70,
                    ADD_BTN_W
                };

                if (rectContains(addBtnRect, fbX, fbY))
                {
                    // Add button tapped
                    fileBrowserToggleAdd(reversedIndex);
                    return true;
                }
                else
                {
                    // Row tapped - select/enter
                    fileBrowserTapRow(reversedIndex);
                    return true;
                }
            }
        }

        // Cancel/Done buttons
        x -= BR_GAP2;
        x -= BR_BTNS_H;
        int half = FBH/2 - 10;

        // Cancel button (left)
        SDL_Rect cancelBtn = {x, 5, BR_BTNS_H, half};
        if (rectContains(cancelBtn, fbX, fbY))
        {
            fileBrowserCancel();
            return true;
        }

        // Done button (right)
        SDL_Rect doneBtn = {x, FBH/2 + 5, BR_BTNS_H, half};
        if (rectContains(doneBtn, fbX, fbY))
        {
            fileBrowserDone();
            return true;
        }

        return false;
    }

    return false;
}


/* ============================================================
   SETTINGS MENU BUTTON TAPS
   Coordinates from settings.cpp
============================================================ */
static bool handleSettingsTap(float fbX, float fbY)
{
    // Settings layout constants (from settings.cpp)
    const int FBW = 1920;
    const int FBH = 1080;
    const int S_MARGIN_TOP = 400;
    const int S_TITLE_H = 80;
    const int S_ROW_H = 160;
    const int S_GAP = 8;
    const int S_BTNS_H = 80;
    const int S_HDR_H = 80;  // For page nav buttons

    int x = FBW - S_MARGIN_TOP;
    x -= S_TITLE_H;
    
    // Page navigation buttons (same as file browser style)
    // [<] button - previous page
    SDL_Rect prevPageBtn = {x + (S_HDR_H - 50)/2, FBH - 40 - 180, 50, 80};
    if (rectContains(prevPageBtn, fbX, fbY))
    {
        settingsPrevPage();  // We'll need to add this function
        return true;
    }
    
    // [>] button - next page
    SDL_Rect nextPageBtn = {x + (S_HDR_H - 50)/2, FBH - 40 - 80, 50, 80};
    if (rectContains(nextPageBtn, fbX, fbY))
    {
        settingsNextPage();  // We'll need to add this function
        return true;
    }

    // Determine which page we're on and what settings to show
    int currentPage = settingsGetCurrentPage();
    
    if (currentPage == 0)  // Page 1
    {
        // Page 1 Settings
        struct SettingRect {
            int id;
            int height;
        };
        SettingRect settings[] = {
            {SETTING_CROSSFADE, S_ROW_H},
            {SETTING_CROSSFADE_TIME, S_ROW_H},
            {SETTING_REPLAYGAIN, S_ROW_H},
            {SETTING_AUTOGAIN, S_ROW_H},
        };

        for (auto& setting : settings)
        {
            x -= (setting.height + S_GAP);
            SDL_Rect rowRect = {x, 0, setting.height, FBH};

            if (rectContains(rowRect, fbX, fbY))
            {
                // Handle different setting types
                switch (setting.id)
                {
                    case SETTING_CROSSFADE:
                        g_settings.crossfadeEnabled = !g_settings.crossfadeEnabled;
                        return true;

                    case SETTING_CROSSFADE_TIME:
                        // Slider handled in drag handler
                        return true;

                    case SETTING_REPLAYGAIN:
                        // Cycle through replay gain modes
                        if (g_settings.replayGainMode == REPLAYGAIN_OFF)
                            g_settings.replayGainMode = REPLAYGAIN_TRACK;
                        else if (g_settings.replayGainMode == REPLAYGAIN_TRACK)
                            g_settings.replayGainMode = REPLAYGAIN_ALBUM;
                        else
                            g_settings.replayGainMode = REPLAYGAIN_OFF;
                        return true;

                    case SETTING_AUTOGAIN:
                        g_settings.autoGainEnabled = !g_settings.autoGainEnabled;
                        return true;
                }
            }
        }
    }
    else if (currentPage == 1)  // Page 2
    {
        // Page 2 Settings
        struct SettingRect {
            int id;
            int height;
        };
        SettingRect settings[] = {
            {SETTING_TOUCH_SENSITIVITY, S_ROW_H},
            {SETTING_TOUCH_SPEED_LIMIT, S_ROW_H},
            {SETTING_STAY_AWAKE, S_ROW_H},
        };

        for (auto& setting : settings)
        {
            x -= (setting.height + S_GAP);
            SDL_Rect rowRect = {x, 0, setting.height, FBH};

            if (rectContains(rowRect, fbX, fbY))
            {
                // Handle different setting types
                switch (setting.id)
                {
                    case SETTING_TOUCH_SENSITIVITY:
                        // Slider handled in drag handler
                        return true;

                    case SETTING_TOUCH_SPEED_LIMIT:
                        // Increment speed limit on tap
                        g_settings.touchSpeedLimit++;
                        if (g_settings.touchSpeedLimit > 10)
                            g_settings.touchSpeedLimit = 1;
                        return true;

                    case SETTING_STAY_AWAKE:
                        g_settings.stayAwakeEnabled = !g_settings.stayAwakeEnabled;
                        return true;
                }
            }
        }
    }

    // Save Settings and Back buttons (same on all pages)
    x -= S_GAP;
    x -= S_BTNS_H;
    int half = FBH/2 - 10;

    // Save Settings button (left)
    SDL_Rect saveBtn = {x, 5, S_BTNS_H, half};
    if (rectContains(saveBtn, fbX, fbY))
    {
        settingsSave();
        settingsClose();
        return true;
    }

    // Back button (right)
    SDL_Rect backBtn = {x, FBH/2 + 5, S_BTNS_H, half};
    if (rectContains(backBtn, fbX, fbY))
    {
        settingsClose();
        return true;
    }

    return false;
}

/* ============================================================
   DRAG HANDLERS — called every frame a finger is held down
============================================================ */
static void handlePlayerDrag(float fbX, float fbY, float startFbX, float startFbY)
{
    const SDL_Rect volBar  = {1551, 421, 40, 264};
    const SDL_Rect panBar  = {1552, 698, 40, 145};
    const SDL_Rect progBar = {1467,  64, 61, 982};
    const SDL_Rect playlistSlider = {45, 50, 70, 950};

    // Playlist slider from ui.cpp drawPlaylistSlider
    // Track: X=208, Y=1020, W=326, H=30
    // Knob: 103x30, positioned within track based on scroll
    // Create expanded hit area to catch touches on/near the knob
    const int trackX = 208;
    const int trackY = 1020;
    const int trackW = 326;
    const int trackH = 50;  // Expanded height for easier touch
    SDL_Rect playlistSliderArea = {
        trackX - 10,      // Expand left
        trackY - 10,      // Expand top
        trackW + 20,      // Expand width
        trackH + 20       // Expand height
    };

    // Determine which zone the drag started in
    if (rectContains(volBar, startFbX, startFbY))
    {
        handleVolumeBar(fbY);
        return;
    }
    if (rectContains(panBar, startFbX, startFbY))
    {
        handlePanSlider(fbY);
        return;
    }
    if (rectContains(progBar, startFbX, startFbY))
    {
        handleProgressBar(fbY);
        return;
    }
    if (rectContains(playlistSliderArea, startFbX, startFbY))
    {
        handlePlaylistSlider(fbX, fbY);  // Pass both coordinates
        return;
    }

    // EQ band sliders — check all 11
    for (int i = 0; i < 11; i++)
    {
        if (rectContains(g_eqBands[i], startFbX, startFbY))
        {
            handleEQBand(i, fbX);
            return;
        }
    }
}

static void handlePlaylistDrag(TouchPoint& touch, float fbX, float fbY)
{
    // If we haven't identified which track is being dragged yet
    if (touch.draggedPlaylistIndex == -1)
    {
        touch.draggedPlaylistIndex = getPlaylistIndexAtPosition(touch.startFbX, touch.startFbY);
        if (touch.draggedPlaylistIndex == -1)
            return; // Not dragging a playlist item
        
        // Select this song in the playlist for visual feedback
        playlistSetCurrentIndex(touch.draggedPlaylistIndex);
    }

    const int totalTracks = playlistGetCount();
    if (totalTracks <= 1) return;  // Nothing to reorder
    
    // === DISTANCE-BASED DRAGGING WITH CONTROLLED SPEED ===
    // Calculate how far finger moved from the current dragged position
    // (not from original grab point - that's key for smooth control)
    
    float currentPos = fbX;
    float draggedSongPos = touch.startFbX;  // Where we originally grabbed
    
    // Calculate distance from current position to where song should be
    float dragDistance = currentPos - draggedSongPos;
    
    // Use sensitivity from settings (default 45.0)
    const float PIXELS_PER_SONG = g_settings.touchSensitivity;
    
    // How many positions should we be from the original grab point?
    int desiredMoveSteps = (int)(dragDistance / PIXELS_PER_SONG);
    
    // Calculate where the song should be now
    // Start from where we grabbed it, add the movement
    int originalGrabIndex = getPlaylistIndexAtPosition(touch.startFbX, touch.startFbY);
    if (originalGrabIndex == -1) originalGrabIndex = touch.draggedPlaylistIndex;
    
    // Target is: where we grabbed + how far we've dragged
    int targetIndex = originalGrabIndex + desiredMoveSteps;
    
    // Clamp to valid range
    targetIndex = clampf(targetIndex, 0, totalTracks - 1);
    
    // === CONTROLLED MOVEMENT ===
    // Use speed limit from settings (default 3)
    const int MAX_SWAPS_PER_FRAME = g_settings.touchSpeedLimit;
    int swaps = 0;
    
    while (targetIndex != touch.draggedPlaylistIndex && swaps < MAX_SWAPS_PER_FRAME)
    {
        if (targetIndex > touch.draggedPlaylistIndex)
        {
            // Moving down
            playlistSwapTracks(touch.draggedPlaylistIndex, touch.draggedPlaylistIndex + 1);
            touch.draggedPlaylistIndex++;
        }
        else
        {
            // Moving up
            playlistSwapTracks(touch.draggedPlaylistIndex, touch.draggedPlaylistIndex - 1);
            touch.draggedPlaylistIndex--;
        }
        swaps++;
    }
    
    // Update scroll position to keep the dragged song visible
    int scroll = playlistGetScroll();
    const int MAX_VISIBLE = 4;
    
    if (touch.draggedPlaylistIndex < scroll)
        playlistSetScroll(touch.draggedPlaylistIndex);
    else if (touch.draggedPlaylistIndex >= scroll + MAX_VISIBLE)
        playlistSetScroll(touch.draggedPlaylistIndex - MAX_VISIBLE + 1);
    
    // Keep the dragged song selected for visual feedback
    if (swaps > 0)
        playlistSetCurrentIndex(touch.draggedPlaylistIndex);
}

static void handleSettingsDrag(float fbX, float fbY, float startFbX, float startFbY)
{
    // Settings slider drag support
    const int FBW = 1920;
    const int FBH = 1080;
    const int S_MARGIN_TOP = 400;
    const int S_TITLE_H = 80;
    const int S_ROW_H = 160;
    const int S_GAP = 8;
    const int S_SLIDER_W = 400;
    const int S_SLIDER_H = 50;

    int currentPage = settingsGetCurrentPage();
    int x = FBW - S_MARGIN_TOP - S_TITLE_H;

    if (currentPage == 0)  // Page 1
    {
        // Skip Crossfade row (no slider)
        x -= (S_ROW_H + S_GAP);
        
        // Crossfade Time slider (second row on Page 1)
        x -= (S_ROW_H + S_GAP);
        
        // Calculate slider track position (matching sDrawSlider in settings.cpp)
        int sliderFBY_centre = FBH / 2;
        int trackFBY = sliderFBY_centre - S_SLIDER_W / 2;
        int trackFBX = x + (S_ROW_H - S_SLIDER_H) / 2;

        // Create hit area for crossfade slider
        SDL_Rect crossfadeSliderArea = {
            trackFBX - 30,        // Expand left
            trackFBY - 20,        // Expand up
            S_SLIDER_H + 60,      // Expand width
            S_SLIDER_W + 40       // Expand height
        };

        if (rectContains(crossfadeSliderArea, startFbX, startFbY))
        {
            // Dragging the crossfade time slider
            float t = (fbY - trackFBY) / (float)S_SLIDER_W;
            t = clampf(t, 0.0f, 1.0f);
            t = 1.0f - t;  // Reversed for display
            g_settings.crossfadeSeconds = 0.5f + t * (10.0f - 0.5f);
        }
    }
    else if (currentPage == 1)  // Page 2
    {
        // Touch Sensitivity slider (first row on Page 2)
        x -= (S_ROW_H + S_GAP);
        
        // Calculate slider track position
        int sliderFBY_centre = FBH / 2;
        int trackFBY = sliderFBY_centre - S_SLIDER_W / 2;
        int trackFBX = x + (S_ROW_H - S_SLIDER_H) / 2;

        // Create hit area for touch sensitivity slider
        SDL_Rect sensitivitySliderArea = {
            trackFBX - 30,
            trackFBY - 20,
            S_SLIDER_H + 60,
            S_SLIDER_W + 40
        };

        if (rectContains(sensitivitySliderArea, startFbX, startFbY))
        {
            // Dragging the touch sensitivity slider
            float t = (fbY - trackFBY) / (float)S_SLIDER_W;
            t = clampf(t, 0.0f, 1.0f);
            t = 1.0f - t;  // Reversed for display
            g_settings.touchSensitivity = 20.0f + t * (80.0f - 20.0f);
        }
    }
}

/* ============================================================
   PUBLIC API
============================================================ */
void touchInit()
{
    // hidInitializeTouchScreen is called automatically by libnx
    // when the applet starts. Nothing extra needed here.
    // This function exists as a hook for future setup if needed.
    printf("[Touch] Touchscreen input initialised\n");
}

void touchUpdate()
{
    // Read the latest touch state from the OS
    // hidGetTouchScreenStates fills the state struct with up to
    // MAX_TOUCHES simultaneous finger positions
    hidGetTouchScreenStates(&g_touchState, 1);
    g_touchCount = (int)g_touchState.count;
}

int touchGetDraggedPlaylistIndex()
{
    // Check all active touches for a playlist drag
    for (int i = 0; i < MAX_TOUCHES; i++)
    {
        if (g_touches[i].active && g_touches[i].moved && g_touches[i].draggedPlaylistIndex >= 0)
        {
            return g_touches[i].draggedPlaylistIndex;
        }
    }
    return -1;  // No playlist item being dragged
}

bool touchHandleInput(bool hasFileBrowser, bool hasSettings)
{
    bool consumed = false;

    // -------------------------------------------------------
    // Build updated touch list from current HID state
    // -------------------------------------------------------
    bool stillActive[MAX_TOUCHES] = {};

    for (int i = 0; i < g_touchCount && i < MAX_TOUCHES; i++)
    {
        const HidTouchState& raw = g_touchState.touches[i];
        float fbX, fbY;
        toFB((int)raw.x, (int)raw.y, fbX, fbY);

        // Find existing touch by ID or create a new slot
        bool found = false;
        for (int j = 0; j < MAX_TOUCHES; j++)
        {
            if (g_touches[j].active && g_touches[j].id == (int)raw.finger_id)
            {
                float dx = fbX - g_touches[j].fbX;
                float dy = fbY - g_touches[j].fbY;
                if (sqrtf(dx*dx + dy*dy) > DRAG_THRESHOLD)
                    g_touches[j].moved = true;
                g_touches[j].fbX = fbX;
                g_touches[j].fbY = fbY;
                stillActive[j]   = true;
                found            = true;
                break;
            }
        }
        if (!found)
        {
            // New finger down — find a free slot
            for (int j = 0; j < MAX_TOUCHES; j++)
            {
                if (!g_touches[j].active)
                {
                    g_touches[j] = { (int)raw.finger_id,
                                     fbX, fbY, fbX, fbY,
                                     true, false, -1 };
                    stillActive[j] = true;
                    break;
                }
            }
        }
    }

    // -------------------------------------------------------
    // Process lifts — a finger that was active but is no
    // longer in the current state has been lifted.
    // Lifts that didn't move = TAP.
    // -------------------------------------------------------
    for (int j = 0; j < MAX_TOUCHES; j++)
    {
        if (!g_touches[j].active) continue;
        if (stillActive[j])
        {
            // Finger still down — handle ongoing drags
            if (g_touches[j].moved)
            {
                if (hasFileBrowser)
                {
                    // No drag support in file browser currently
                }
                else if (hasSettings)
                {
                    // Settings slider drag
                    handleSettingsDrag(g_touches[j].fbX,
                                     g_touches[j].fbY,
                                     g_touches[j].startFbX,
                                     g_touches[j].startFbY);
                    consumed = true;
                }
                else
                {
                    // Player screen drags
                    handlePlayerDrag(g_touches[j].fbX,
                                     g_touches[j].fbY,
                                     g_touches[j].startFbX,
                                     g_touches[j].startFbY);

                    // Check for playlist reordering drag
                    handlePlaylistDrag(g_touches[j],
                                      g_touches[j].fbX,
                                      g_touches[j].fbY);
                    consumed = true;
                }
            }
        }
        else
        {
            // Finger lifted — was it a tap?
            float dx = g_touches[j].fbX - g_touches[j].startFbX;
            float dy = g_touches[j].fbY - g_touches[j].startFbY;
            bool  isTap = (sqrtf(dx*dx + dy*dy) < TAP_THRESHOLD);

            if (isTap)
            {
                float tx = g_touches[j].startFbX;
                float ty = g_touches[j].startFbY;

                if (hasFileBrowser)
                {
                    consumed = handleFileBrowserTap(tx, ty);
                }
                else if (hasSettings)
                {
                    consumed = handleSettingsTap(tx, ty);
                }
                else
                {
                    // Player screen tap
                    consumed = handlePlayerTap(tx, ty);
                }
            }

            // Clear the slot
            g_touches[j] = {};
        }
    }

    return consumed;
}
