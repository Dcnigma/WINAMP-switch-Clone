#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <switch.h>

void settingsOpen();
void settingsClose();
bool settingsIsOpen();

void settingsRender(SDL_Renderer* renderer, TTF_Font* font);
void settingsHandleInput(PadState* pad);
