#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <switch.h>
#include <stdbool.h>
// Open the browser (scans sdmc:/ root)
void fileBrowserOpen();

// Returns true if browser UI is currently active
bool fileBrowserIsActive();

// Handle input (D-Pad / A / B)
void fileBrowserUpdate(PadState* pad);

// Draw the browser overlay
void fileBrowserRender(SDL_Renderer* renderer, TTF_Font* font);

// Save current playlist to sdmc:/winamp/playlist.m3u
void fileBrowserSavePlaylist();

// --- New helpers for selected files ---
bool fileBrowserDidSelectFolder();   // returns true once after user confirms selection
const char** fileBrowserGetSelectedFiles();  // returns array of selected file paths
int fileBrowserGetSelectedFileCount();       // returns number of selected files
void fileBrowserClearSelection();            // reset selection state
