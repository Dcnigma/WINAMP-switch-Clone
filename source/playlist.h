#pragma once
#include <SDL.h>
#include <SDL_ttf.h>

// Scroll support
extern int playlistScroll;          // allows main.cpp to modify scroll position
void playlistScrollUp();
void playlistScrollDown();
void playlistClear();
void playlistSetCurrentIndex(int index);

int playlistGetScroll();        // current scroll offset
int playlistGetMaxVisible();    // how many songs fit on screen

// Swap two tracks in the playlist (for drag-to-reorder)
void playlistSwapTracks(int index1, int index2);
// Set scroll position directly (for slider dragging)
void playlistSetScroll(int scroll);

// Draw playlist UI
void renderPlaylist(SDL_Renderer* renderer, TTF_Font* font);

// Add a song to playlist (full path)
void playlistAdd(const char* path);

// Get track count
int playlistGetCount();

// Get track path by index
const char* playlistGetTrack(int index);
