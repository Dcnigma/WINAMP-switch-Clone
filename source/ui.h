#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

// Draw a filled rectangle
void drawRect(SDL_Renderer* renderer, SDL_Rect r, Uint8 rC, Uint8 gC, Uint8 bC, Uint8 aC);

// Draw vertical text (90° rotation)
void drawVerticalText(SDL_Renderer* renderer, TTF_Font* font, const char* text, SDL_Rect rect);

// Render the UI
void uiRender(SDL_Renderer* renderer, TTF_Font* font, SDL_Texture* skin, const char* songText);
