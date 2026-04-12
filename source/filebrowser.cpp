#include "filebrowser.h"
#include "playlist.h"
#include "mp3.h"
#include "flac.h"

#include <switch.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>

#define FB_MAX_ITEMS 128
#define FB_PATH_LEN  512

typedef struct {
    char name[256];
    char fullpath[FB_PATH_LEN];
    bool isDir;
} BrowserItem;

static BrowserItem items[FB_MAX_ITEMS];
static int itemCount = 0;
static int selected  = 0;
static int openCooldown = 0;

static char currentPath[FB_PATH_LEN] = "sdmc:/";
static bool active = false;

/* ---------------------------------------------------- */
/* FILE TYPE HELPERS                                     */
/* ---------------------------------------------------- */
static bool isMp3(const char* name)
{
    const char* ext = strrchr(name, '.');
    return ext && strcasecmp(ext, ".mp3") == 0;
}

static bool isFlac(const char* name)
{
    const char* ext = strrchr(name, '.');
    return ext && strcasecmp(ext, ".flac") == 0;
}

static bool isAudioFile(const char* name)
{
    return isMp3(name) || isFlac(name);
}

// Add a single audio file to the playlist using the right loader.
static void importFile(const char* path)
{
    if (isFlac(path))
        flacAddToPlaylist(path);
    else
        mp3AddToPlaylist(path);
}

/* ---------------------------------------------------- */
/* DIRECTORY SCAN                                        */
/* ---------------------------------------------------- */
static void scanDirectory(const char* path)
{
    itemCount = 0;
    selected  = 0;

    strlcpy(currentPath, path, sizeof(currentPath));

    DIR* dir = opendir(path);
    if (!dir) return;

    // Add ".." unless we're at the SD root
    if (strcmp(path, "sdmc:/") != 0 && itemCount < FB_MAX_ITEMS)
    {
        strlcpy(items[itemCount].name,     "..", sizeof(items[itemCount].name));
        strlcpy(items[itemCount].fullpath, path, sizeof(items[itemCount].fullpath));
        items[itemCount].isDir = true;
        itemCount++;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) && itemCount < FB_MAX_ITEMS)
    {
        if (ent->d_name[0] == '.') continue;

        snprintf(items[itemCount].fullpath, sizeof(items[itemCount].fullpath),
                 "%s/%s", path, ent->d_name);
        strlcpy(items[itemCount].name, ent->d_name, sizeof(items[itemCount].name));

        struct stat st;
        items[itemCount].isDir =
            (stat(items[itemCount].fullpath, &st) == 0) && S_ISDIR(st.st_mode);

        itemCount++;
    }

    closedir(dir);
}

/* ---------------------------------------------------- */
/* FOLDER IMPORT                                         */
/* ---------------------------------------------------- */
static void importFolder(const char* path)
{
    // Collect all audio files in sorted order so the playlist
    // matches what you'd expect from a file manager.
    DIR* dir = opendir(path);
    if (!dir) return;

    std::vector<std::string> mp3Files;
    std::vector<std::string> flacFiles;
    struct dirent* ent;

    while ((ent = readdir(dir)))
    {
        if (isMp3(ent->d_name))
            mp3Files.emplace_back(ent->d_name);
        else if (isFlac(ent->d_name))
            flacFiles.emplace_back(ent->d_name);
    }
    closedir(dir);

    // Nothing audio in this folder — bail without clearing the existing playlist
    if (mp3Files.empty() && flacFiles.empty())
        return;

    // Check whether this exact folder is already loaded to avoid duplicate scans.
    // We consider it loaded only if BOTH loaders agree (handles mixed folders).
    bool alreadyLoaded = mp3IsFolderLoaded(path) && flacIsFolderLoaded(path);
    if (alreadyLoaded)
        return;

    // Cancel any in-flight background scans before rebuilding the playlist
    mp3CancelAllScans();
    flacCancelAllScans();
    playlistClear();
    mp3ClearMetadata();
    flacClearMetadata();

    char folderKey[512];
    snprintf(folderKey, sizeof(folderKey), "%s/", path);
    mp3LoadCache(folderKey);
    flacLoadCache(folderKey);

    playlistScroll = 0;

    // Merge and sort all audio files together by filename so playback order
    // is alphabetical regardless of format (e.g. 01.flac, 02.mp3 interleave).
    std::vector<std::string> allFiles;
    for (auto& f : mp3Files)  allFiles.push_back(f);
    for (auto& f : flacFiles) allFiles.push_back(f);
    std::sort(allFiles.begin(), allFiles.end());

    for (auto& f : allFiles)
    {
        std::string fullPath = std::string(path) + "/" + f;
        importFile(fullPath.c_str());
    }

    // Mark both loaders so a second enter of the same folder is a no-op
    mp3SetLoadedFolder(path);
    flacSetLoadedFolder(path);
}

/* ---------------------------------------------------- */
/* PUBLIC API                                            */
/* ---------------------------------------------------- */
void fileBrowserOpen()
{
    playlistClear();
    mp3CancelAllScans();
    flacCancelAllScans();
    mp3ClearMetadata();
    flacClearMetadata();
    scanDirectory("sdmc:/");
    playlistScroll = 0;
    active = true;
    openCooldown = 10;
}

bool fileBrowserIsActive()
{
    return active;
}

void fileBrowserUpdate(PadState* pad)
{
    if (openCooldown > 0) { openCooldown--; return; }
    if (!active) return;

    u64 down = padGetButtonsDown(pad);

    /* ---- Navigation ---- */
    if (down & HidNpadButton_Up)
        selected = (selected > 0) ? selected - 1 : selected;

    if (down & HidNpadButton_Down)
        selected = (selected < itemCount - 1) ? selected + 1 : selected;

    /* ---- Close browser ---- */
    if (down & HidNpadButton_X)
    {
        active = false;
        return;
    }

    /* ---- Navigate into folders / go up ---- */
    if (down & HidNpadButton_A)
    {
        BrowserItem* it = &items[selected];

        if (strcmp(it->name, "..") == 0)
        {
            char temp[FB_PATH_LEN];
            snprintf(temp, sizeof(temp), "%s", currentPath);
            char* slash = strrchr(temp, '/');
            if (slash && slash != temp)
            {
                *slash = '\0';
                scanDirectory(temp);
            }
            return;
        }

        if (it->isDir)
            scanDirectory(it->fullpath);

        // Single audio file: A just selects, B imports (see below)
    }

    /* ---- Import into playlist ---- */
    if (down & HidNpadButton_B)
    {
        BrowserItem* it = &items[selected];

        if (strcmp(it->name, "..") == 0)
        {
            // Import the whole current folder
            importFolder(currentPath);
            active = false;
        }
        else if (it->isDir)
        {
            // Import the entire sub-folder
            importFolder(it->fullpath);
            active = false;
        }
        else if (isAudioFile(it->name))
        {
            // Import just this one file
            importFile(it->fullpath);
        }

        return;
    }
}

/* ---------------------------------------------------- */
/* RENDER                                                */
/* ---------------------------------------------------- */
void fileBrowserRender(SDL_Renderer* renderer, TTF_Font* font)
{
    if (!active) return;

    SDL_Rect bg = { 0, 0, 1920, 1080 };
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(renderer, &bg);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color sel   = {255, 200,   0, 255};
    SDL_Color gray  = {160, 160, 160, 255}; // directories shown slightly dimmer

    for (int i = 0; i < itemCount; i++)
    {
        SDL_Color color;
        if (i == selected)
            color = sel;
        else if (items[i].isDir)
            color = gray;
        else
            color = white;

        // Show a small format tag after the filename so the user can tell
        // MP3 and FLAC apart at a glance.  Directories show nothing extra.
        char label[300];
        if (!items[i].isDir && isFlac(items[i].name))
            snprintf(label, sizeof(label), "%s  [FLAC]", items[i].name);
        else if (!items[i].isDir && isMp3(items[i].name))
            snprintf(label, sizeof(label), "%s  [MP3]", items[i].name);
        else
            snprintf(label, sizeof(label), "%s", items[i].name);

        SDL_Surface* s = TTF_RenderText_Solid(font, label, color);
        if (!s) continue;

        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_Rect dst = { 220, 130 + i * 30, s->w, s->h };
        SDL_RenderCopy(renderer, t, NULL, &dst);

        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    }
}
