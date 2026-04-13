#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <switch.h>
#include <stdbool.h>

void fileBrowserOpen();
bool fileBrowserIsActive();
void fileBrowserUpdate(PadState* pad);
void fileBrowserRender(SDL_Renderer* renderer, TTF_Font* font);

// Touchscreen scroll: dir=-1 (back) or +1 (forward), jump=items (0=use default PAGE_JUMP=6)
void fileBrowserScrollPage(int dir, int jump = 0);
