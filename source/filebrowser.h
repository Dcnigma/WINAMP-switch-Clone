#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <switch.h>
#include <stdbool.h>

void fileBrowserOpen();
void fileBrowserOpenadd();
bool fileBrowserIsActive();
void fileBrowserUpdate(PadState* pad);
void fileBrowserRender(SDL_Renderer* renderer, TTF_Font* font);

// Handle tap on file/folder row
void fileBrowserTapRow(int visibleIndex);

// Toggle add/remove for file at visible index
void fileBrowserToggleAdd(int visibleIndex);

// Cancel button handler
void fileBrowserCancel();

// Done button handler
void fileBrowserDone();

// Touchscreen scroll: dir=-1 (back) or +1 (forward), jump=items (0=use default PAGE_JUMP=6)
void fileBrowserScrollPage(int dir, int jump = 0);

// Check which screen is active
bool fileBrowserIsMenu();
bool fileBrowserIsBrowse();

// Get number of items in browse screen (for dynamic row calculation)
int fileBrowserGetItemCount();
