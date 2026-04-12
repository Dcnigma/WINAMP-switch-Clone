#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <switch.h>
#include <stdbool.h>

// Open the file browser (shows the Add File / Add URL / Cancel menu first)
void fileBrowserOpen();

// Returns true while any browser screen is active
bool fileBrowserIsActive();

// Call every frame to handle controller input
void fileBrowserUpdate(PadState* pad);

// Call every frame to draw the browser overlay
void fileBrowserRender(SDL_Renderer* renderer, TTF_Font* font);
