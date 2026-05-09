#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include "ui.h"
#include "mp3.h"
#include "flac.h"
#include "ogg.h"
#include "wav.h"
#include "eq.h"
#include "filebrowser.h"
#include "playlist.h"
#include "player.h"
#include "player_state.h"
#include "controller.h"

#include "settings.h"
#include "settings_state.h"
#include "touchscreen.h"

#define FB_W 1920
#define FB_H 1080

static u64 lastPlaybackActivityTick = 0;
static bool stayAwakeActive = true;
int selectedBand = 1;

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

void updateStayAwakeLogic()
{
    // Check if stay awake is enabled in settings
    if (!g_settings.stayAwakeEnabled)
    {
        // Disabled - ensure auto-sleep is active
        if (stayAwakeActive)
        {
            appletSetIdleTimeDetectionExtension((AppletIdleTimeDetectionExtension)0);
            stayAwakeActive = false;
        }
        return;
    }

    // Stay awake is enabled - normal logic
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
    flacStartBackgroundScanner();
    oggStartBackgroundScanner();
    wavStartBackgroundScanner();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    TTF_Init();
    IMG_Init(IMG_INIT_PNG);
    playerInit();

    SDL_Window* window = SDL_CreateWindow(
        "Winamp Switch",
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
    SDL_Texture* texEQMAIN = IMG_LoadTexture(renderer, "romfs:/skins/EQMAIN.png");

    mp3AddToPlaylist("romfs:/song.mp3");
    playlistScroll = 0;


    controllerInit();

    // Tell the applet we want to handle our own exit/focus events.
    // Without this, sphaira closing your app can leave threads running.
    appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);


    while (appletMainLoop())
    {
        updateStayAwakeLogic();
        //New controller inputs
        controllerUpdate();
        PadState* pad = controllerGetPad();
        u64 down = padGetButtonsDown(pad);
        u64 up   = padGetButtonsUp(pad);
        u64 now  = svcGetSystemTick();

        // -------------------------------------------------------
        // EXCLUSIVE INPUT FOCUS
        // Only one screen owns input at a time.
        // Priority: filebrowser > settings > player
        // Nothing leaks through to a lower-priority handler.
        // -------------------------------------------------------

        if (down & HidNpadButton_Plus)
            break;  // always quit regardless of focus

        if (fileBrowserIsActive())
        {
            // Filebrowser owns ALL input — player and settings get nothing.
            fileBrowserUpdate(pad);
        }
        else if (settingsIsOpen())
        {
            // Settings owns ALL input — player gets nothing.
            // StickL while settings is open is ignored (already open).
            settingsHandleInput(pad);
        }
        else
        {
            // Player / playlist owns input.
            // Open filebrowser with B.
            if (down & HidNpadButton_B)
            {
                playerStop();
                fileBrowserOpen();
            }
            // Open settings with StickL.
            else if (down & HidNpadButton_StickL)
            {
                settingsOpen();
            }
            else
            {
                // Full player controls — A, Up, Down, Left, Right etc.
                // are safe here because no overlay is active.
                controllerHandlePlayerControls();
            }
        }
        touchUpdate();
        touchHandleInput(fileBrowserIsActive(), settingsIsOpen());
        updateAutoEQ();
        playerUpdate();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        char songText[256] = "WINamp By DcNigma";
        int idx = playerGetCurrentTrackIndex();
        if (idx >= 0)
        {
            const Mp3MetadataEntry* md = mp3GetTrackMetadata(idx);
            if (!md) md = flacGetTrackMetadata(idx);
            if (!md) md = oggGetTrackMetadata(idx);
            if (!md) md = wavGetTrackMetadata(idx);
            if (md)
                snprintf(songText, sizeof(songText), "%d. %s - %s",
                         idx + 1,
                         md->artist[0] ? md->artist : "Unknown",
                         md->title[0]  ? md->title  : "Unknown");
        }

        uiRender(renderer, font, fontBig, skin,
                 texProgIndicator, texVolume, texPan,
                 texPlaylistKnob, texCbuttons, texSHUFREP,texEQMAIN,
                 songText);

        renderPlaylist(renderer, font);
        fileBrowserRender(renderer, font);
        settingsRender(renderer, font);
        SDL_RenderPresent(renderer);
    }

    // -------------------------------------------------------
    // SHUTDOWN - Order matters! Stop in reverse init order.
    //
    // IMPORTANT: We deliberately do NOT call SDL_Quit() here.
    // On Switch under sphaira/HBL, SDL_Quit() triggers an
    // internal bsdsocket cleanup that null-dereferences when
    // the socket was already partially torn down by the OS.
    // The OS will clean up remaining resources when the process
    // exits. Skipping SDL_Quit() is safe and prevents the crash.
    // -------------------------------------------------------

    // 1. Stop all background scanner threads first
    mp3StopBackgroundScanner();
    flacStopBackgroundScanner();
    oggStopBackgroundScanner();
    wavStopBackgroundScanner();

    // 2. Stop and fully shutdown the player (closes audio device)
    playerStop();
    playerShutdown();

    // 3. Restore normal sleep/wake behavior
    appletSetIdleTimeDetectionExtension((AppletIdleTimeDetectionExtension)0);


    // 4. Destroy SDL textures
    SDL_DestroyTexture(skin);
    SDL_DestroyTexture(texProgIndicator);
    SDL_DestroyTexture(texVolume);
    SDL_DestroyTexture(texPan);
    SDL_DestroyTexture(texPlaylistKnob);
    SDL_DestroyTexture(texCbuttons);
    SDL_DestroyTexture(texSHUFREP);
    SDL_DestroyTexture(texEQMAIN);

    // 5. Close fonts
    TTF_CloseFont(font);
    TTF_CloseFont(fontBig);

    // 6. Destroy renderer and window
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    // 7. Quit SDL subsystems individually - avoid SDL_Quit()
    //    SDL_Quit() triggers bsdsocket teardown which crashes under sphaira.
    //    Instead quit only what we explicitly initialised.
    IMG_Quit();
    TTF_Quit();
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    // Do NOT call SDL_Quit() here

    // 8. romfsExit LAST
    romfsExit();

    return 0;
}
