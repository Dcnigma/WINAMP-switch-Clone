#include "playlist.h"
#include "ui.h"
#include <vector>
#include <string>
#include "mp3.h"
#include "flac.h"
#include "ogg.h"
#include "wav.h"
#include <stdio.h>
#include "filebrowser.h"
#include "player.h"

static std::vector<std::string> playlist;

int playlistScroll = 0;              // top visible item
static int currentIndex = 0;         // selected track

// 🔥 ONE place that defines how many tracks fit on screen
static const int MAX_VISIBLE_TRACKS = 4;



int playlistGetScroll()
{
    return playlistScroll;
}

int playlistGetMaxVisible()
{
    return MAX_VISIBLE_TRACKS;
}



/* ---------- Selection control ---------- */
void playlistSetCurrentIndex(int index)
{
    if (index >= 0 && index < playlistGetCount())
        currentIndex = index;
}

int playlistGetCurrentIndex()
{
    return currentIndex;
}

/* ---------- Find playing song by file path ---------- */
int playlistGetPlayingIndex()
{
    // Get the file path of what's currently playing
    const char* playingPath = playerGetCurrentTrackPath();
    
    // If nothing is playing, return -1
    if (!playingPath)
        return -1;
    
    // Search through the playlist to find where that file is NOW
    // This works even if:
    // - Files were added before it (shifting its index)
    // - It was drag-and-dropped to a new position
    // - The playlist was reordered
    for (int i = 0; i < (int)playlist.size(); i++)
    {
        if (playlist[i] == playingPath)
            return i;  // Found it at this position!
    }
    
    // File not found in playlist
    // This can happen if the playing song was removed from the playlist
    return -1;
}



/* ---------- Add / Get ---------- */
void playlistAdd(const char* path)
{
    if (!path) return;
    playlist.push_back(path);
    // Adding to end doesn't affect player's index - no update needed
}

int playlistGetCount()
{
    return playlist.size();
}

const char* playlistGetTrack(int index)
{
    if (index < 0 || index >= (int)playlist.size()) return NULL;
    return playlist[index].c_str();
}



/* ---------- Scroll Logic ---------- */
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

    if (currentIndex < count - 1)
        currentIndex++;

    if (currentIndex >= playlistScroll + MAX_VISIBLE_TRACKS)
        playlistScroll = currentIndex - MAX_VISIBLE_TRACKS + 1;
}

void playlistSwapTracks(int index1, int index2)
{
    if (index1 < 0 || index2 < 0) return;
    if (index1 >= playlistGetCount() || index2 >= playlistGetCount()) return;
    if (index1 == index2) return;

    // Get the currently playing track index BEFORE swap
    int playingIndex = playerGetCurrentTrackIndex();

    // Swap the playlist entries
    std::string temp = playlist[index1];
    playlist[index1] = playlist[index2];
    playlist[index2] = temp;

    // Update currentIndex (the selected/highlighted song)
    if (currentIndex == index1)
        currentIndex = index2;
    else if (currentIndex == index2)
        currentIndex = index1;
    
    // Update player's track index if the playing song moved
    // This keeps the player pointing to the correct song
    if (playingIndex == index1)
        playerUpdateTrackIndex(index2);
    else if (playingIndex == index2)
        playerUpdateTrackIndex(index1);
}

void playlistSetScroll(int scroll)
{
    playlistScroll = scroll;
    if (playlistScroll < 0) playlistScroll = 0;
    int maxScroll = playlistGetCount() - MAX_VISIBLE_TRACKS;
    if (maxScroll < 0) maxScroll = 0;
    if (playlistScroll > maxScroll) playlistScroll = maxScroll;
}


/* ---------- Clear ---------- */
void playlistClear()
{
    playlist.clear();
    playlistScroll = 0;
    currentIndex = 0;
}



/* ---------- Rendering ---------- */
static void formatTime(int seconds, char* out, size_t outSize)
{
    int m = seconds / 60;
    int s = seconds % 60;
    snprintf(out, outSize, "%02d:%02d", m, s);
}

void renderPlaylist(SDL_Renderer* renderer, TTF_Font* font)
{
    SDL_Rect trackTitleArea = {230, 55, 72, 870};
    SDL_Rect trackTimeArea  = {230, 908, 65, 91};

    int count = playlistGetCount();

    // Clamp scroll
    if (playlistScroll > count - MAX_VISIBLE_TRACKS)
        playlistScroll = (count - MAX_VISIBLE_TRACKS > 0) ? count - MAX_VISIBLE_TRACKS : 0;
    if (playlistScroll < 0) playlistScroll = 0;

    int visible = (count < MAX_VISIBLE_TRACKS) ? count : MAX_VISIBLE_TRACKS;

    // Get the ACTUAL position of the playing song in the current playlist
    // This finds it by file path, so it works even after drag-and-drop or file additions
    int playingIndex = playlistGetPlayingIndex();
    int elapsed = playerGetElapsedSeconds();

    for (int i = 0; i < visible; i++)
    {
        int idx = playlistScroll + i;
        const char* trackPath = playlistGetTrack(idx);
        if (!trackPath) continue;

        const Mp3MetadataEntry* md = mp3GetTrackMetadata(idx);
        if (!md) md = flacGetTrackMetadata(idx);
        if (!md) md = oggGetTrackMetadata(idx);
        if (!md) md = wavGetTrackMetadata(idx);
        // If still null, render a fallback line using the filename


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

        titleRect.x += (MAX_VISIBLE_TRACKS - 1 - i) * 75;
        timeRect.x  += (MAX_VISIBLE_TRACKS - 1 - i) * 75;

        // Highlight selected
        if (idx == playlistGetCurrentIndex())
        {
            SDL_SetRenderDrawColor(renderer, 0, 128, 255, 60);
            SDL_Rect highlight = titleRect;
            highlight.w = 70;
            highlight.h = 945;
            SDL_RenderFillRect(renderer, &highlight);
        }

        SDL_Color color = (idx == playingIndex)
            ? SDL_Color{255,255,255,255}
            : SDL_Color{0,255,0,255};

        SDL_Rect leftAlignedRect = titleRect;
        leftAlignedRect.x += 2;
        drawVerticalText(renderer, font, line, leftAlignedRect, color);

        char timeText[16] = "--:--";
        if (md && md->durationSeconds > 0)
            formatTime(md->durationSeconds, timeText, sizeof(timeText));

        if (idx == playingIndex)
        {
            char live[32];
            snprintf(live, sizeof(live), "%02d:%02d", elapsed/60, elapsed%60);
            drawVerticalText(renderer, font, live, timeRect, color);
        }
        else
        {
            drawVerticalText(renderer, font, timeText, timeRect, color);
        }
    }
}
