#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

void drawRect(SDL_Renderer* renderer, SDL_Rect r, Uint8 rC, Uint8 gC, Uint8 bC, Uint8 aC);

void uiRender(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* fontBig, SDL_Texture* skin, SDL_Texture* texProgIndicator, const char* songText);

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
