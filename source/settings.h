#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <switch.h>

void settingsOpen();
void settingsClose();
bool settingsIsOpen();
void settingsSave();

// Page navigation
int settingsGetCurrentPage();
void settingsPrevPage();
void settingsNextPage();

void settingsRender(SDL_Renderer* renderer, TTF_Font* font);
void settingsHandleInput(PadState* pad);
