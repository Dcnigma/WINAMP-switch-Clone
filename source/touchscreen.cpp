#include "touchscreen.h"
#include "player.h"
#include "player_state.h"
#include "playlist.h"
#include "filebrowser.h"
#include "settings.h"
#include "ui.h"
#include "eq.h"

#include <switch.h>
#include <stdio.h>
#include <math.h>

extern bool autoEQEnabled;
extern int  selectedBand;

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
   Used for volume bar, pan slider, progress bar, EQ bands.
============================================================ */
#define MAX_TOUCHES 10

struct TouchPoint {
    int   id;         // finger ID from HID
    float fbX, fbY;   // current position in framebuffer coords
    float startFbX, startFbY;
    bool  active;
    bool  moved;      // true once the finger has moved > DRAG_THRESHOLD px
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

// EQ band sliders — bands 1-10 are in eqBand2..eqBand11
// Each rect is {730, Y, 340, 33}; knob moves left→right for cut→boost
// Band value range is -12 .. +12 dB
static const SDL_Rect g_eqBands[11] = {
    {730,  90, 340, 33},  // [0] = preamp (band index used for setBand offset)
    {730, 315, 340, 33},  // [1] = band 1
    {730, 387, 340, 33},  // [2] = band 2
    {730, 457, 340, 33},  // [3] = band 3
    {730, 532, 340, 33},  // [4] = band 4
    {730, 603, 340, 33},  // [5] = band 5
    {730, 671, 340, 33},  // [6] = band 6
    {730, 743, 340, 33},  // [7] = band 7
    {730, 813, 340, 33},  // [8] = band 8
    {730, 885, 340, 33},  // [9] = band 9
    {730, 953, 340, 33},  // [10]= band 10
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
static void handlePlaylistTap(float fbX, float fbY)
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
                playlistSetCurrentIndex(trackIdx);
                playerPlay(trackIdx);
            }
            return;
        }
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

    // --- Shuffle / Repeat ---
    const SDL_Rect shuffleButton = {1357, 642,  72, 182};
    const SDL_Rect repeatButton  = {1357, 825,  72, 115};

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
        fileBrowserOpen();
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
    const SDL_Rect kbpsInfo = {1639, 426, 57, 74};
    const SDL_Rect kHzInfo  = {1639, 600, 57, 55};
    if (rectContains(kbpsInfo, fbX, fbY) || rectContains(kHzInfo, fbX, fbY))
    {
        settingsOpen();
        return true;
    }

    // --- Playlist row tap ---
    handlePlaylistTap(fbX, fbY);

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
                                     true, false };
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
                // Only the player screen has draggable sliders
                if (!hasFileBrowser && !hasSettings)
                {
                    handlePlayerDrag(g_touches[j].fbX,
                                     g_touches[j].fbY,
                                     g_touches[j].startFbX,
                                     g_touches[j].startFbY);
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
                    // Filebrowser scroll arrows — ^ and v buttons
                    // These sit in the "// SELECT FILES" strip on the right.
                    // Their rects from filebrowser render: ~{HDR_X, sbx..sbyUp/Dn}
                    // We expose fileBrowserScrollPage() for exactly this purpose.
                    // Rough touch zones for the two scroll buttons:
                    const SDL_Rect scrollUp   = {1740, 650, 72, 72};
                    const SDL_Rect scrollDown = {1740, 740, 72, 72};

                    if (rectContains(scrollUp, tx, ty))
                    {
                        fileBrowserScrollPage(-1, 0);
                        consumed = true;
                    }
                    else if (rectContains(scrollDown, tx, ty))
                    {
                        fileBrowserScrollPage(+1, 0);
                        consumed = true;
                    }
                    // All other taps in filebrowser are handled by
                    // fileBrowserUpdate() via controller-style input.
                    // TODO: when full touch nav is added, dispatch row taps here.
                }
                else if (hasSettings)
                {
                    // Settings taps — for now just close on tap outside
                    // Full row-tap support can be added later similarly to
                    // handlePlaylistTap() once button rects are exported.
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
