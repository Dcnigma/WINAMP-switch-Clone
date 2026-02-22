#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include "ui.h"
#include "mp3.h"
#include "filebrowser.h"
#include "playlist.h"
#include "player.h"
#include "player_state.h"

#define FB_W 1920
#define FB_H 1080

static u64 lastPlaybackActivityTick = 0;
static bool stayAwakeActive = true;

// ----------------------------------
// NEW: Scrub state (delta-time based)
// ----------------------------------
struct ScrubState
{
    bool active = false;
    bool forward = false;
    u64 startTicks = 0;
};

static ScrubState g_scrub;

// ----------------------------------

void updateStayAwakeLogic()
{
    u64 now = svcGetSystemTick();

    if (playerIsPlaying())
    {
        lastPlaybackActivityTick = now;

        if (!stayAwakeActive)
        {
            appletSetIdleTimeDetectionExtension(AppletIdleTimeDetectionExtension_ExtendedUnsafe);
            appletOverrideAutoSleepTimeAndDimmingTime(0, 0, 0, 0);
            stayAwakeActive = true;
        }
    }
    else
    {
        const u64 timeout = 5ULL * 60ULL * 19200000ULL;
        if (stayAwakeActive && (now - lastPlaybackActivityTick) > timeout)
        {
            appletSetIdleTimeDetectionExtension((AppletIdleTimeDetectionExtension)0);
            stayAwakeActive = false;
        }
    }
}

int main()
{
    romfsInit();

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);
    playerInit();

    SDL_Window* window = SDL_CreateWindow(
        "Winamp Switch Demo",
        0, 0, FB_W, FB_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    TTF_Font* font = TTF_OpenFont("romfs:/fonts/arial.ttf", 32);
    TTF_Font* fontBig = TTF_OpenFont("romfs:/fonts/arial.ttf", 82);

    SDL_Texture* skin = IMG_LoadTexture(renderer, "romfs:/skins/default_skin.png");
    SDL_Texture* texProgIndicator = IMG_LoadTexture(renderer, "romfs:/skins/prgBindicator.png");
    SDL_Texture* texVolume = IMG_LoadTexture(renderer, "romfs:/skins/VOLUME.png");
    SDL_Texture* texPan = IMG_LoadTexture(renderer, "romfs:/skins/BALANCE.png");
    SDL_Texture* texPlaylistKnob = IMG_LoadTexture(renderer, "romfs:/skins/PlaylistKnob.png");
    SDL_Texture* texCbuttons = IMG_LoadTexture(renderer, "romfs:/skins/CBUTTONS.png");
    SDL_Texture* texSHUFREP = IMG_LoadTexture(renderer, "romfs:/skins/SHUFREP.png");

    playlistClear();
    mp3ClearMetadata();
    mp3AddToPlaylist("romfs:/song.mp3");
    playlistScroll = 0;

    PadState pad;
    padConfigureInput(1, HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadFullKey);
    padInitializeDefault(&pad);

    while (appletMainLoop())
    {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        u64 up   = padGetButtonsUp(&pad);
        u64 now  = svcGetSystemTick();

        updateStayAwakeLogic();
        // File browser open

        if (down & HidNpadButton_Plus)
             break;

         // File browser open
         if (fileBrowserIsActive())
         {
             fileBrowserUpdate(&pad);
         }
         else if (down & HidNpadButton_B)
         {
             // Clear playlist first
             playerStop();
             playlistClear();
             fileBrowserOpen();
         }

         // Play first track with A
         if (!fileBrowserIsActive() && (down & HidNpadButton_A))
         {
             if (playlistGetCount() > 0)
                 playerPlay(playlistGetCurrentIndex());
         }



         // Scroll playlist
         if (down & HidNpadButton_Up)   playlistScrollUp();
         if (down & HidNpadButton_Down) playlistScrollDown();

         playerUpdate();


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
        // NEW: Winamp-style scrubbing
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
        if (down & HidNpadButton_StickLLeft) playerSetPan(playerGetPan() - 0.1f);
        if (down & HidNpadButton_StickLRight)playerSetPan(playerGetPan() + 0.1f);

        // ---------------------------
        // Exit
        // ---------------------------
        if (down & HidNpadButton_Plus)
            break;

        playerUpdate();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        char songText[256] = "Stopped";
        int idx = playerGetCurrentTrackIndex();
        if (idx >= 0)
        {
            const Mp3MetadataEntry* md = mp3GetTrackMetadata(idx);
            if (md)
                snprintf(songText, sizeof(songText), "%d. %s - %s",
                         idx + 1,
                         md->artist[0] ? md->artist : "Unknown",
                         md->title[0]  ? md->title  : "Unknown");
        }

        uiRender(renderer, font, fontBig, skin,
                 texProgIndicator, texVolume, texPan,
                 texPlaylistKnob, texCbuttons, texSHUFREP,
                 songText);

        renderPlaylist(renderer, font);
        fileBrowserRender(renderer, font);

        SDL_RenderPresent(renderer);
    }

    playerStop();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    romfsExit();
    return 0;
}
