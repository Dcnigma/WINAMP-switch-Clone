#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include "ui.h"
#include "mp3.h"
#include "eq.h"
#include "filebrowser.h"
#include "playlist.h"
#include "player.h"
#include "player_state.h"
#include "controller.h"

#define FB_W 1920
#define FB_H 1080

static u64 lastPlaybackActivityTick = 0;
static bool stayAwakeActive = true;
static int selectedBand = 1;

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
    mp3StartBackgroundScanner();
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

    mp3AddToPlaylist("romfs:/song.mp3");
    playlistScroll = 0;

    controllerInit();


    while (appletMainLoop())
    {
        //New controller inputs
        controllerUpdate();

        controllerHandlePlayerControls();

        PadState* pad = controllerGetPad();

        u64 down = padGetButtonsDown(pad);
        u64 up   = padGetButtonsUp(pad);
        u64 now  = svcGetSystemTick();

        updateStayAwakeLogic();
        // File browser open

        if (down & HidNpadButton_Plus)
             break;

         // File browser open
         if (fileBrowserIsActive())
         {
             fileBrowserUpdate(pad);
         }
         else if (down & HidNpadButton_B)
         {
             playerStop();
             playlistClear();
             mp3CancelAllScans();
             mp3ClearMetadata();
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
        // Enabled DSP
        // ---------------------------
        if (down & HidNpadButton_Minus)
          g_equalizer.toggle();

        if (down & HidNpadButton_ZL)
            g_equalizer.setPreamp(g_equalizer.getPreamp() - 1.0f);

        if (down & HidNpadButton_ZR)
            g_equalizer.setPreamp(g_equalizer.getPreamp() + 1.0f);

        if (down & HidNpadButton_StickR)
        {
            autoEQEnabled = !autoEQEnabled;
        }

        if (down & HidNpadButton_Up)
        {
            selectedBand--;
            if (selectedBand < 1) selectedBand = 10;
        }

        if (down & HidNpadButton_Down)
        {
            selectedBand++;
            if (selectedBand > 10) selectedBand = 1;
        }

        if (down & HidNpadButton_Left)
        {
            g_equalizer.setBand(selectedBand,
                g_equalizer.getBand(selectedBand) - 1.0f);
        }

        if (down & HidNpadButton_Right)
        {
            g_equalizer.setBand(selectedBand,
                g_equalizer.getBand(selectedBand) + 1.0f);
        }

        updateAutoEQ();
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

    mp3StopBackgroundScanner(); 
    playerStop();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    romfsExit();
}
