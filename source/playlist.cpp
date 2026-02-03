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

int playlistGetScroll()
{
    return playlistScroll;
}

// how many rows your UI shows at once
int playlistGetMaxVisible()
{
    return 10; // <-- CHANGE if your UI shows a different amount
}


static void formatTime(int seconds, char* out, size_t outSize)
{
    int m = seconds / 60;
    int s = seconds % 60;
    snprintf(out, outSize, "%02d:%02d", m, s);
}

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

void renderPlaylist(SDL_Renderer* renderer, TTF_Font* font)
{
    SDL_Rect trackTitleArea = {230, 55, 72, 870};
    SDL_Rect trackTimeArea  = {230, 908, 65, 91};

    int count = playlistGetCount();
    int maxVisible = 4;

    // Clamp scroll
    if (playlistScroll > count - maxVisible)
        playlistScroll = (count - maxVisible > 0) ? count - maxVisible : 0;
    if (playlistScroll < 0) playlistScroll = 0;

    int visible = (count < maxVisible) ? count : maxVisible;

    int playingIndex = playerGetCurrentTrackIndex();
    int elapsed = playerGetElapsedSeconds();
    int total = playerGetTrackLength();

    for (int i = 0; i < visible; i++)
    {
        int idx = playlistScroll + i;
        const char* trackPath = playlistGetTrack(idx);
        if (!trackPath) continue;

        const Mp3MetadataEntry* md = mp3GetTrackMetadata(idx);

        // --- Track title ---
        char line[256];
        if (md && (md->artist[0] || md->title[0]))
            snprintf(line, sizeof(line), "%d. %.20s - %.40s", idx + 1, md->artist, md->title);
        else
        {
            const char* name = strrchr(trackPath, '/');
            name = name ? name + 1 : trackPath;
            snprintf(line, sizeof(line), "%d. %s", idx + 1, name);
        }

        SDL_Rect titleRect = trackTitleArea;
        SDL_Rect timeRect  = trackTimeArea;

        titleRect.x += (maxVisible - 1 - i) * 75;
        timeRect.x  += (maxVisible - 1 - i) * 75;

        // --- Selection highlight ---
        if (idx == playlistGetCurrentIndex())
        {
            SDL_SetRenderDrawColor(renderer, 0, 128, 255, 60); // blue selection
            SDL_Rect highlight = titleRect;
            highlight.w = 70;
            highlight.h = 945;
            SDL_RenderFillRect(renderer, &highlight);
        }

        // --- Text colors ---
        SDL_Color titleColor = (idx == playingIndex) ? SDL_Color{255,255,255,255} : SDL_Color{0,255,0,255};
        SDL_Color timeColor  = titleColor;

        // --- Draw track title ---
        SDL_Rect leftAlignedRect = titleRect;
        leftAlignedRect.x += 2; // small padding
        drawVerticalText(renderer, font, line, leftAlignedRect, titleColor);

        // --- Draw track time ---
        char timeText[16] = "--:--";

        if (md && md->durationSeconds > 0)
            formatTime(md->durationSeconds, timeText, sizeof(timeText));

        // If currently playing, overwrite with live elapsed / total
        if (idx == playingIndex)
        {
            char live[32];
            snprintf(live, sizeof(live), "%02d:%02d",
                     elapsed/60, elapsed%60);

            drawVerticalText(renderer, font, live, timeRect, timeColor);
        }
        else
        {
            drawVerticalText(renderer, font, timeText, timeRect, timeColor);
        }
    }
}
