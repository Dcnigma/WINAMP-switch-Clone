#pragma once
#include <switch.h>
#include <stdbool.h>

// Call once at startup after hidInitialize
void touchInit();

// Call every frame before processing touch — reads raw touch state
void touchUpdate();

// Call every frame in the same exclusive-focus chain as controller input.
// Pass which overlay is currently active so touch is routed correctly.
//   hasFileBrowser : fileBrowserIsActive()
//   hasSettings    : settingsIsOpen()
// Returns true if a touch event was consumed this frame.
bool touchHandleInput(bool hasFileBrowser, bool hasSettings);
