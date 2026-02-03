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


// Draw playlist UI
void renderPlaylist(SDL_Renderer* renderer, TTF_Font* font);

// Add a song to playlist (full path)
void playlistAdd(const char* path);

// Get track count
int playlistGetCount();

// Get track path by index
const char* playlistGetTrack(int index);
