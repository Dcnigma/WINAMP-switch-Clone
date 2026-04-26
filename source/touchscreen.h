#ifndef TOUCHSCREEN_H
#define TOUCHSCREEN_H

#include <stdbool.h>

/* ============================================================
   TOUCHSCREEN INPUT API
   
   Provides touch input handling for the Switch music player.
   Supports:
   - Tap detection for buttons and UI elements
   - Drag support for sliders (volume, pan, progress, EQ, playlist scroll)
   - Playlist reordering via drag-and-drop
   - Full touchscreen support for file browser and settings menus
============================================================ */

// Initialize touchscreen system (called once at startup)
void touchInit();

// Update touch state (called every frame to read new touch data)
void touchUpdate();

// Process touch input and dispatch to appropriate handlers
// hasFileBrowser: true if file browser overlay is active
// hasSettings: true if settings overlay is active
// Returns: true if touch input was consumed/handled
bool touchHandleInput(bool hasFileBrowser, bool hasSettings);

// Get the index of the currently dragged playlist item (-1 if none)
// Used for visual feedback (highlighting the dragged song)
int touchGetDraggedPlaylistIndex();

#endif // TOUCHSCREEN_H
