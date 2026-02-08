#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include "ui.h"
#include "mp3.h"
#include "filebrowser.h"
#include "playlist.h"
#include "player.h"


#define FB_W 1920
#define FB_H 1080

static u64 lastPlaybackActivityTick = 0;
static bool stayAwakeActive = true;


void updateStayAwakeLogic()
{
    u64 now = svcGetSystemTick();

    if (playerIsPlaying())
    {
        lastPlaybackActivityTick = now;

        if (!stayAwakeActive)
        {
            appletSetIdleTimeDetectionExtension(AppletIdleTimeDetectionExtension_ExtendedUnsafe);
            appletOverrideAutoSleepTimeAndDimmingTime(0,0,0,0);
            stayAwakeActive = true;
        }
    }
    else
    {
        // 5 minutes = 5 * 60 seconds * 19.2MHz ticks
        const u64 timeout = 5ULL * 60ULL * 19200000ULL;

        if (stayAwakeActive && (now - lastPlaybackActivityTick) > timeout)
        {
            // Back to normal system dim/sleep behavior
            appletSetIdleTimeDetectionExtension((AppletIdleTimeDetectionExtension)0);
            stayAwakeActive = false;
        }

    }
}


int main()
{

    // --- Initialize ---
    romfsInit();

    // Prevent screen dimming / sleep while app is running


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
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); // important for transparency

    // --- Load resources ---
    TTF_Font* font = TTF_OpenFont("romfs:/fonts/arial.ttf", 32);
    if (!font) printf("Failed to load font!\n");

    TTF_Font* fontBig = TTF_OpenFont("romfs:/fonts/arial.ttf", 82);
    if (!fontBig) printf("Failed to load BIG font!\n");

    SDL_Texture* skin = IMG_LoadTexture(renderer, "romfs:/skins/default_skin.png");
    if (!skin) printf("Failed to load skin: %s\n", SDL_GetError());

    SDL_Texture* texProgIndicator = IMG_LoadTexture(renderer, "romfs:/skins/prgBindicator.png");
    if (!texProgIndicator)
    {
        printf("Failed to load prgBindicator.png: %s\n", IMG_GetError());
    }

    SDL_Texture* texVolume = IMG_LoadTexture(renderer, "romfs:/skins/VOLUME.png");
    if (!texVolume)
        printf("Failed to load volume.png: %s\n", IMG_GetError());

    SDL_Texture* texPan = IMG_LoadTexture(renderer, "romfs:/skins/BALANCE.png");
    if (!texPan)
        printf("Failed to load volume.png: %s\n", IMG_GetError());

    SDL_Texture* texPlaylistKnob = IMG_LoadTexture(renderer, "romfs:/skins/PlaylistKnob.png");
    if (!texPlaylistKnob)
        printf("Failed to load volume.png: %s\n", IMG_GetError());

    // --- At boot ---
    playlistClear();
    mp3ClearMetadata();

    mp3AddToPlaylist("romfs:/song.mp3");
    playlistScroll = 0;


    // --- Configure controller ---
    PadState pad;
    padConfigureInput(1,
        HidNpadStyleTag_NpadHandheld |
        HidNpadStyleTag_NpadFullKey
    );
    padInitializeDefault(&pad);

    SDL_Event e;
    while (SDL_PollEvent(&e)) {}


    while (appletMainLoop())
    {
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);

        updateStayAwakeLogic();

        if (down & HidNpadButton_StickLLeft)
            playerSetPan(playerGetPan() - 0.1f);

        if (down & HidNpadButton_StickLRight)
            playerSetPan(playerGetPan() + 0.1f);


        // ---------------------------
        // Volume Control (Left Stick)
        // ---------------------------
        const float VOL_STEP = 0.05f; // 5% per press

        if (down & HidNpadButton_StickLUp)
        {
            playerAdjustVolume(+VOL_STEP);
        }

        if (down & HidNpadButton_StickLDown)
        {
            playerAdjustVolume(-VOL_STEP);
        }

        playerUpdate();
        // Exit
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
            playlistClear();
            fileBrowserOpen();
        }

        // Play first track with A
        if (!fileBrowserIsActive() && (down & HidNpadButton_A))
        {
            if (playlistGetCount() > 0)
                playerPlay(playlistGetCurrentIndex());
        }


        // Stop with X
        if (down & HidNpadButton_X)
        {
            playerStop();
        }
        // Scroll playlist
        if (down & HidNpadButton_Up)   playlistScrollUp();
        if (down & HidNpadButton_Down) playlistScrollDown();

        playerUpdate();

        // --- Render ---
        SDL_SetRenderDrawColor(renderer, 0,0,0,255);
        SDL_RenderClear(renderer);


        // --- Display currently playing song metadata ---
        char songText[256] = "Stopped";

        int playingIndex = playerGetCurrentIndex();

        if (playingIndex >= 0 && playingIndex < mp3GetPlaylistCount())
        {
            const Mp3MetadataEntry* md = mp3GetTrackMetadata(playingIndex);
            if (md)
            {
                // Include playlist index like OG Winamp
                snprintf(songText, sizeof(songText),
                         "%d. %.120s - %.120s",
                         playingIndex + 1,                  // playlist number
                         md->artist[0] ? md->artist : "Unknown",
                         md->title[0]  ? md->title  : "Unknown");
            }
        }


        uiRender(renderer, font, fontBig, skin, texProgIndicator, texVolume, texPan, texPlaylistKnob, songText);   // UI background
//        uiRender(renderer, font, smallFont, texProgIndicator, currentTitle);

        renderPlaylist(renderer, font);            // Playlist
        fileBrowserRender(renderer, font);         // Browser overlay

        SDL_RenderPresent(renderer);
        SDL_PollEvent(NULL);
    }

    // --- Cleanup ---
    playerStop();
    //playerShutdown();
    SDL_Delay(50);
    if (font) TTF_CloseFont(font);
    if (skin) SDL_DestroyTexture(skin);
    if (fontBig) TTF_CloseFont(fontBig);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    romfsExit();

    return 0;
}
