#include "playlist.h"
#include "ui.h"
#include <vector>
#include <string>
#include "mp3.h"
#include <stdio.h>
#include "filebrowser.h"
#include "player.h"

static std::vector<std::string> playlist;
int playlistScroll = 0;            // top visible item
static int currentIndex = 0;       // selected track (for playback highlight)

/* ---------- Selection control (independent from scroll) ---------- */
void playlistSetCurrentIndex(int index)
{
    if (index >= 0 && index < playlistGetCount())
        currentIndex = index;
}

int playlistGetCurrentIndex()
{
    return currentIndex;
}

// Add file to playlist
void playlistAdd(const char* path)
{
    if (!path) return;
    playlist.push_back(path);
}

// Get total tracks
int playlistGetCount()
{

    return playlist.size();

}

// Get track path
const char* playlistGetTrack(int index)
{
    if (index < 0 || index >= (int)playlist.size()) return NULL;
    return playlist[index].c_str();
}

// Scroll support
void playlistScrollUp()
{
    if (currentIndex > 0)
        currentIndex--;

    if (currentIndex < playlistScroll)
        playlistScroll = currentIndex;
}

void playlistScrollDown()
{
    int count = playlistGetCount();
    int maxVisible = 4;

    if (currentIndex < count - 1)
        currentIndex++;

    if (currentIndex >= playlistScroll + maxVisible)
        playlistScroll = currentIndex - maxVisible + 1;
}


void playlistClear()
{
    playlist.clear();
    playlistScroll = 0;
    currentIndex = 0;
}



// --- Render playlist ---
// void renderPlaylist(SDL_Renderer* renderer, TTF_Font* font)
// {
//     SDL_Rect trackTitleArea = {230, 33, 72, 870};
//     SDL_Rect trackTimeArea  = {230, 908, 65, 91};
//
//     int count = playlistGetCount();
//     int maxVisible = 4;
//
//     if (playlistScroll > count - maxVisible)
//         playlistScroll = (count - maxVisible > 0) ? count - maxVisible : 0;
//     if (playlistScroll < 0)
//         playlistScroll = 0;
//
//     int visible = (count < maxVisible) ? count : maxVisible;
//
//     for (int i = 0; i < visible; i++)
//     {
//         int idx = playlistScroll + i;
//
//         char line[256];
//         const Mp3MetadataEntry* md = mp3GetTrackMetadata(idx);
//
//         if (md && (md->artist[0] || md->title[0]))
//             snprintf(line, sizeof(line), "%d. %.20s - %.20s", idx + 1, md->artist, md->title);
//         else
//         {
//             const char* full = playlistGetTrack(idx);
//             if (!full) continue;
//
//             const char* name = strrchr(full, '/');
//             name = name ? name + 1 : full;
//             snprintf(line, sizeof(line), "%d. %s", idx + 1, name);
//         }
//
//         SDL_Rect titleRect = trackTitleArea;
//         SDL_Rect timeRect  = trackTimeArea;
//
//         titleRect.x += (maxVisible - 1 - i) * 75;
//         timeRect.x  += (maxVisible - 1 - i) * 75;
//
//         // --- Selection highlight (blue) ---
//         bool selectedItem = (idx == currentIndex);
//         if (selectedItem)
//         {
//             SDL_SetRenderDrawColor(renderer, 0, 128, 255, 60); // blue selection
//             SDL_Rect highlight = titleRect;
//             highlight.w = 72;
//             highlight.h = 870;
//             SDL_RenderFillRect(renderer, &highlight);
//         }
//
//         // --- Determine text color ---
//         SDL_Color textColor;
//         if (idx == playerGetCurrentIndex())
//             textColor = {255, 255, 255, 255}; // currently playing = white
//         else
//             textColor = {0, 255, 0, 255};     // default = green
//
//         // --- LEFT-ALIGN FIX ---
//         SDL_Rect leftAlignedRect = titleRect;
//         leftAlignedRect.x += 2; // small padding from left side
//         drawVerticalText(renderer, font, line, leftAlignedRect, textColor);
//
//         leftAlignedRect = timeRect;
//         leftAlignedRect.x += 2; // small padding
//         drawVerticalText(renderer, font, "--:--", leftAlignedRect, textColor);
//     }
// }


void renderPlaylist(SDL_Renderer* renderer, TTF_Font* font)
{
    SDL_Rect trackTitleArea = {230, 55, 72, 870};
    SDL_Rect trackTimeArea  = {230, 908, 65, 91};

    int count = playlistGetCount();
    int maxVisible = 4;

    if (playlistScroll > count - maxVisible)
        playlistScroll = (count - maxVisible > 0) ? count - maxVisible : 0;
    if (playlistScroll < 0)
        playlistScroll = 0;

    int visible = (count < maxVisible) ? count : maxVisible;

    for (int i = 0; i < visible; i++)
    {
        int idx = playlistScroll + i;

        char line[256];
        const Mp3MetadataEntry* md = mp3GetTrackMetadata(idx);

        if (md && (md->artist[0] || md->title[0]))
            snprintf(line, sizeof(line), "%d. %.20s - %.20s", idx + 1, md->artist, md->title);
        else
        {
            const char* full = playlistGetTrack(idx);
            if (!full) continue;

            const char* name = strrchr(full, '/');
            name = name ? name + 1 : full;
            snprintf(line, sizeof(line), "%d. %s", idx + 1, name);
        }

        SDL_Rect titleRect = trackTitleArea;
        SDL_Rect timeRect  = trackTimeArea;

        titleRect.x += (maxVisible - 1 - i) * 75;
        timeRect.x  += (maxVisible - 1 - i) * 75;

        // --- Selection highlight (blue) ---
        bool selectedItem = (idx == currentIndex);
        if (selectedItem)
        {
            SDL_SetRenderDrawColor(renderer, 0, 128, 255, 60); // blue selection
            SDL_Rect highlight = titleRect;
            highlight.w = 70;
            highlight.h = 945;
            SDL_RenderFillRect(renderer, &highlight);
        }

        // --- Text color ---
        SDL_Color titleColor  = (idx == playerGetCurrentIndex()) ? SDL_Color{255,255,255,255} : SDL_Color{0,255,0,255};
        SDL_Color timeColor   = (idx == playerGetCurrentIndex()) ? SDL_Color{255,255,255,255} : SDL_Color{0,255,0,255};

        // --- Left-aligned text ---
        SDL_Rect leftAlignedRect = titleRect;
        leftAlignedRect.x += 2; // small padding
        drawVerticalText(renderer, font, line, leftAlignedRect, titleColor);

        leftAlignedRect = timeRect;
        leftAlignedRect.x += 2;
        drawVerticalText(renderer, font, "--:--", leftAlignedRect, timeColor);
    }
}
