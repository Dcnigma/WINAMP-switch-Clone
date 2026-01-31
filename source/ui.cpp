#include "ui.h"
#include "mp3.h"
#include "player.h"
#include "playlist.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <stdio.h>
#include <string.h>

#define FB_W 1920
#define FB_H 1080

// --- Draw filled rectangle with alpha ---
void drawRect(SDL_Renderer* renderer, SDL_Rect r, Uint8 rC, Uint8 gC, Uint8 bC, Uint8 aC)
{
    SDL_SetRenderDrawColor(renderer, rC, gC, bC, aC);
    SDL_RenderFillRect(renderer, &r);
}

void drawVerticalText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      const char* text,
                      SDL_Rect rect,
                      SDL_Color color,
                      int paddingX,          // manual horizontal offset
                      int paddingY,          // manual vertical offset
                      VerticalAlign alignY)  // vertical alignment inside rect
{
    if (!font || !text) return;

    SDL_Surface* s = TTF_RenderText_Solid(font, text, color);
    if (!s) return;

    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
    if (!t) { SDL_FreeSurface(s); return; }

    SDL_Rect dst;
    dst.w = s->w;
    dst.h = s->h;

    // Horizontal position (after rotation)
    dst.x = rect.x + rect.w - dst.h + paddingX;

    // Vertical alignment inside the UI rectangle
    switch (alignY)
    {
        case ALIGN_TOP:
            dst.y = rect.y + paddingY;
            break;

        case ALIGN_CENTER:
            dst.y = rect.y + (rect.h - dst.w) / 2 + paddingY;
            break;

        case ALIGN_BOTTOM:
            dst.y = rect.y + rect.h - dst.w - paddingY;
            break;
    }

    SDL_Point center = {0, 0};
    SDL_RenderCopyEx(renderer, t, NULL, &dst, 90.0, &center, SDL_FLIP_NONE);

    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

// Backward-compatible wrapper (old calls still work)
void drawVerticalText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      const char* text,
                      SDL_Rect rect,
                      SDL_Color color)
{
    drawVerticalText(renderer, font, text, rect, color,
                     16,          // default paddingX
                     0,           // default paddingY
                     ALIGN_TOP);  // default alignment
}

void formatTime(int seconds, char* out, size_t size)
{
    int m = seconds / 60;
    int s = seconds % 60;
    snprintf(out, size, "%02d:%02d", m, s);
}


// --- Render full UI ---
void uiRender(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* fontBig, SDL_Texture* skin, const char* songText)
{

    // --- Live playtime string ---
    char liveTime[16];
    int elapsed = playerGetElapsedSeconds();
    snprintf(liveTime, sizeof(liveTime), "%02d:%02d", elapsed / 60, elapsed % 60);

    // Clear screen
    SDL_RenderClear(renderer);

    // Draw skin background
    if (skin)
    {
        SDL_Rect dst = {0,0,FB_W,FB_H};
        SDL_RenderCopy(renderer, skin, NULL, &dst);
    }

    // --- UI RECTANGLES ---
    SDL_Rect topBar        = {1837,   0,  83,1080};
    SDL_Rect mainPlayer    = {1287,   0, 558,1080};
    SDL_Rect eqSection     = {650,    0, 654,1080};
    SDL_Rect playlist      = {0,      0, 636,1080};
    SDL_Rect progBar       = {1467,  64,  61, 982};
    SDL_Rect songInfo      = {1721, 429, 68, 614};
    SDL_Rect playtimeInfo  = {1694, 178,100,221};
    SDL_Rect kbpsInfo      = {1639, 426, 57, 74};
    SDL_Rect kHzInfo       = {1639, 600, 57, 55};
    SDL_Rect playlistFiles = {215,   50,310,950};

    SDL_Rect prevButton    = {1340,  60,100, 90};
    SDL_Rect playButton    = {1340, 151,100, 90};
    SDL_Rect pauseButton   = {1340, 242,100, 90};
    SDL_Rect stopButton    = {1340, 332,100, 90};
    SDL_Rect nextButton    = {1340, 426,100, 90};
    SDL_Rect ejectButton   = {1340, 532,100, 90};
    SDL_Rect shuffleButton = {1357, 642, 72,182};
    SDL_Rect repeatButton  = {1357, 825, 72,115};

    SDL_Rect eqBand1       = {720,   91,346, 33};
    SDL_Rect eqBand2       = {720,  315,346, 33};
    SDL_Rect eqBand3       = {720,  387,346, 33};
    SDL_Rect eqBand4       = {720,  457,346, 33};
    SDL_Rect eqBand5       = {720,  532,346, 33};
    SDL_Rect eqBand6       = {720,  603,346, 33};
    SDL_Rect eqBand7       = {720,  671,346, 33};
    SDL_Rect eqBand8       = {720,  743,346, 33};
    SDL_Rect eqBand9       = {720,  813,346, 33};
    SDL_Rect eqBand10      = {720,  885,346, 33};
    SDL_Rect eqBand11      = {720,  953,346, 33};

    SDL_Rect eqPreset1     = {1112,  53, 73,104};
    SDL_Rect eqPreset2     = {1112, 153, 73,131};
    SDL_Rect eqPreset3     = {1112, 851, 73,170};

    SDL_Rect volumeSlider  = {1551, 421,40,264};
    SDL_Rect panSlider     = {1551, 698,40,145};

    SDL_Rect addPlaylist   = {70,  42,100,100};
    SDL_Rect rmPlaylist    = {70, 158,100,100};
    SDL_Rect selPlaylist   = {70, 270,100,100};
    SDL_Rect miscPlaylist  = {70, 383,100,100};
    SDL_Rect ListOptions   = {70, 893,100,100};
    SDL_Rect Duration      = {45, 765,50,100};

    // --- Draw rectangles with transparency ---
    drawRect(renderer, topBar, 200,0,0,150);
    drawRect(renderer, mainPlayer, 0,100,200,150);
    drawRect(renderer, eqSection, 0,200,100,150);
    drawRect(renderer, playlist, 200,200,0,150);
    drawRect(renderer, progBar, 150,0,200,150);
    drawRect(renderer, songInfo, 100,100,100,150);
    drawRect(renderer, playtimeInfo, 0,200,100,150);
    drawRect(renderer, kbpsInfo, 200,200,0,150);
    drawRect(renderer, kHzInfo, 150,0,200,150);
    drawRect(renderer, playlistFiles, 100,100,100,150);

    drawRect(renderer, prevButton, 255,0,0,150);
    drawRect(renderer, nextButton, 0,255,0,150);
    drawRect(renderer, playButton, 255,0,0,150);
    drawRect(renderer, stopButton, 0,255,0,150);
    drawRect(renderer, pauseButton, 255,0,0,150);
    drawRect(renderer, ejectButton, 0,255,0,150);
    drawRect(renderer, shuffleButton, 255,0,0,150);
    drawRect(renderer, repeatButton, 0,255,0,150);

    drawRect(renderer, eqBand1, 0,0,255,150);
    drawRect(renderer, eqBand2, 0,255,255,150);
    drawRect(renderer, eqBand3, 255,0,255,150);
    drawRect(renderer, eqBand4, 255,255,0,150);
    drawRect(renderer, eqBand5, 0,255,255,150);
    drawRect(renderer, eqBand6, 255,0,255,150);
    drawRect(renderer, eqBand7, 255,255,0,150);
    drawRect(renderer, eqBand8, 100,100,200,150);
    drawRect(renderer, eqBand9, 0,255,255,150);
    drawRect(renderer, eqBand10, 255,0,255,150);
    drawRect(renderer, eqBand11, 100,100,200,150);

    drawRect(renderer, eqPreset1, 200,100,100,150);
    drawRect(renderer, eqPreset2, 100,200,100,150);
    drawRect(renderer, eqPreset3, 100,100,100,150);

    drawRect(renderer, volumeSlider, 150,150,0,150);
    drawRect(renderer, panSlider, 0,150,150,150);

    drawRect(renderer, addPlaylist, 150,150,0,150);
    drawRect(renderer, rmPlaylist, 0,150,150,150);
    drawRect(renderer, selPlaylist, 150,150,0,150);
    drawRect(renderer, miscPlaylist, 0,150,150,150);
    drawRect(renderer, ListOptions, 150,150,0,150);
    drawRect(renderer, Duration, 150,150,0,150);

    // --- Vertical text with green color ---
    SDL_Color green = {0, 255, 0, 255};

//    drawVerticalText(renderer, font, songText, songInfo, green);
      drawVerticalText(renderer, font, songText, songInfo, green,
                   16,      // small horizontal padding
                   10,      // small top padding
                   ALIGN_TOP);



    drawVerticalText(renderer, fontBig, liveTime, playtimeInfo, green,
                     85,      // horizontal push
                     0,
                     ALIGN_CENTER);

    drawVerticalText(renderer, font, "192", kbpsInfo, green,
                 20,      // small horizontal padding
                 10,      // small top padding
                 ALIGN_TOP);

    drawVerticalText(renderer, font, "44", kHzInfo, green,
                 20,      // small horizontal padding
                 10,      // small top padding
                 ALIGN_TOP);


     drawVerticalText(renderer, font, liveTime, Duration, green,
                 25,      // small horizontal padding
                 10,      // small top padding
                 ALIGN_TOP);

    // --- Playlist ---
    renderPlaylist(renderer, font);

  //  SDL_RenderPresent(renderer);
}
