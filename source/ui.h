#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

void spectrumReset();

void drawRect(SDL_Renderer* renderer, SDL_Rect r, Uint8 rC, Uint8 gC, Uint8 bC, Uint8 aC);

void uiRender(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* fontBig, SDL_Texture* skin, SDL_Texture* texProgIndicator, SDL_Texture* texVolume, SDL_Texture* texPan, SDL_Texture* texPlaylistKnob, SDL_Texture* texCbuttons, SDL_Texture* texSHUFREP, const char* songText);


// ----------------------------------------------------
// UI input abstraction
// ----------------------------------------------------
enum UIButton
{
    UI_BTN_PLAY,
    UI_BTN_PAUSE,
    UI_BTN_NEXT,
    UI_BTN_PREV
};

// Notify UI of a logical button press (for animation feedback)
void uiNotifyButtonPress(UIButton btn);

enum VerticalAlign
{
    ALIGN_TOP,
    ALIGN_CENTER,
    ALIGN_BOTTOM
};

void drawVerticalText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      const char* text,
                      SDL_Rect rect,
                      SDL_Color color,
                      int paddingX,
                      int paddingY,
                      VerticalAlign alignY);

// Old compatibility version
void drawVerticalText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      const char* text,
                      SDL_Rect rect,
                      SDL_Color color);
