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



int main()
{
    // --- Initialize ---
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
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND); // important for transparency

    // --- Load resources ---
    TTF_Font* font = TTF_OpenFont("romfs:/fonts/arial.ttf", 24);
    if (!font) printf("Failed to load font!\n");

    SDL_Texture* skin = IMG_LoadTexture(renderer, "romfs:/skins/default_skin.png");
    if (!skin) printf("Failed to load skin: %s\n", SDL_GetError());

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


        uiRender(renderer, font, skin, songText);   // UI background
        renderPlaylist(renderer, font);            // Playlist
        fileBrowserRender(renderer, font);         // Browser overlay

        SDL_RenderPresent(renderer);
        SDL_PollEvent(NULL);
    }

    // --- Cleanup ---
    if (font) TTF_CloseFont(font);
    if (skin) SDL_DestroyTexture(skin);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    romfsExit();

    return 0;
}
