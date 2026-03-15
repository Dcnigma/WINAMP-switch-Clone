#include "controller.h"
#include "ui.h"
#include "mp3.h"
#include "eq.h"
#include "filebrowser.h"
#include "playlist.h"
#include "player.h"
#include "player_state.h"
#include "controller.h"

static PadState g_pad;

extern bool autoEQEnabled;
//extern int selectedBand;
// static int selectedBand = 1;

struct ScrubState
{
    bool active = false;
    bool forward = false;
    u64 startTicks = 0;
};

static ScrubState g_scrub;

void controllerInit()
{
    padConfigureInput(
        1,
        HidNpadStyleTag_NpadHandheld |
        HidNpadStyleTag_NpadFullKey |
        HidNpadStyleTag_NpadJoyDual |
        HidNpadStyleTag_NpadJoyLeft |
        HidNpadStyleTag_NpadJoyRight
    );

    padInitializeDefault(&g_pad);
}

void controllerUpdate()
{
    padUpdate(&g_pad);
}

PadState* controllerGetPad()
{
    return &g_pad;
}

void controllerHandlePlayerControls()
{
    u64 down = padGetButtonsDown(&g_pad);
    u64 up   = padGetButtonsUp(&g_pad);
    u64 now  = svcGetSystemTick();

    // ---------------------------
    // Playback controls
    // ---------------------------

    if (down & HidNpadButton_A)
    {
        uiNotifyButtonPress(UI_BTN_PLAY);

        if (playerIsPaused())
            playerTogglePause();
        else
            playerPlay(playlistGetCurrentIndex());
    }

    if (down & HidNpadButton_Y)
    {
        uiNotifyButtonPress(UI_BTN_PAUSE);
        playerTogglePause();
    }

    if (down & HidNpadButton_X)
        playerStop();

    if (down & HidNpadButton_StickRRight)
    {
        uiNotifyButtonPress(UI_BTN_NEXT);
        playerNext();
    }

    if (down & HidNpadButton_StickRLeft)
    {
        uiNotifyButtonPress(UI_BTN_PREV);

        if (playerGetPosition() > PREV_RESTART_THRESHOLD)
            playerSeek(0.0f);
        else
            playerPrev();
    }

    if (down & HidNpadButton_StickRUp)
        playerCycleRepeat();

    if (down & HidNpadButton_StickRDown)
        playerToggleShuffle();

    // ---------------------------
    // Scrubbing
    // ---------------------------

    if (down & HidNpadButton_R)
    {
        g_scrub = { true, true, now };
    }
    else if (down & HidNpadButton_L)
    {
        g_scrub = { true, false, now };
    }

    if (g_scrub.active)
    {
        if ((up & HidNpadButton_R) || (up & HidNpadButton_L))
        {
            g_scrub.active = false;
        }
        else
        {
            float heldSec = (now - g_scrub.startTicks) / 19200000.0f;

            float step = 1.0f;

            if (heldSec > 2.0f)      step = 8.0f;
            else if (heldSec > 0.5f) step = 3.0f;

            float pos = playerGetPosition();

            pos += g_scrub.forward ? step : -step;

            if (pos < 0.0f) pos = 0.0f;

            playerSeek(pos);
        }
    }

    // ---------------------------
    // Volume & pan
    // ---------------------------

    if (down & HidNpadButton_StickLUp)   playerAdjustVolume(+0.05f);
    if (down & HidNpadButton_StickLDown) playerAdjustVolume(-0.05f);

    if (down & HidNpadButton_StickLLeft)
        playerSetPan(playerGetPan() - 0.1f);

    if (down & HidNpadButton_StickLRight)
        playerSetPan(playerGetPan() + 0.1f);

    // ---------------------------
    // Equalizer
    // ---------------------------
    //
    // if (down & HidNpadButton_Minus)
    //     g_equalizer.toggle();
    //
    // if (down & HidNpadButton_ZL)
    //     g_equalizer.setPreamp(g_equalizer.getPreamp() - 1.0f);
    //
    // if (down & HidNpadButton_ZR)
    //     g_equalizer.setPreamp(g_equalizer.getPreamp() + 1.0f);
    //
    // if (down & HidNpadButton_StickR)
    //     autoEQEnabled = !autoEQEnabled;
    //
    // if (down & HidNpadButton_Up)
    // {
    //     selectedBand--;
    //     if (selectedBand < 1) selectedBand = 10;
    // }
    //
    // if (down & HidNpadButton_Down)
    // {
    //     selectedBand++;
    //     if (selectedBand > 10) selectedBand = 1;
    // }
    //
    // if (down & HidNpadButton_Left)
    // {
    //     g_equalizer.setBand(
    //         selectedBand,
    //         g_equalizer.getBand(selectedBand) - 1.0f
    //     );
    // }
    //
    // if (down & HidNpadButton_Right)
    // {
    //     g_equalizer.setBand(
    //         selectedBand,
    //         g_equalizer.getBand(selectedBand) + 1.0f
    //     );
    // }
}
