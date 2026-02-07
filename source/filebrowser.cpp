#include "filebrowser.h"
#include "playlist.h"
#include "mp3.h"

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

/* ---------- Helpers ---------- */

static bool isMp3(const char* name)
{
    const char* ext = strrchr(name, '.');
    return ext && strcasecmp(ext, ".mp3") == 0;
}
static void scanDirectory(const char* path)
{
    itemCount = 0;
    selected  = 0;

    // Copy current path safely
    strlcpy(currentPath, path, sizeof(currentPath));

    DIR* dir = opendir(path);
    if (!dir) return;

    // Add ".." unless at root
    if (strcmp(path, "sdmc:/") != 0 && itemCount < FB_MAX_ITEMS)
    {
        strlcpy(items[itemCount].name, "..", sizeof(items[itemCount].name));
        strlcpy(items[itemCount].fullpath, path, sizeof(items[itemCount].fullpath));
        items[itemCount].isDir = true;
        itemCount++;
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) && itemCount < FB_MAX_ITEMS)
    {
        if (ent->d_name[0] == '.') continue;

        // Build full path safely
        snprintf(items[itemCount].fullpath, sizeof(items[itemCount].fullpath),
                 "%s/%s", path, ent->d_name);

        // Copy name safely
        strlcpy(items[itemCount].name, ent->d_name, sizeof(items[itemCount].name));

        struct stat st;
        if (stat(items[itemCount].fullpath, &st) == 0)
            items[itemCount].isDir = S_ISDIR(st.st_mode);
        else
            items[itemCount].isDir = false;

        itemCount++;
    }

    closedir(dir);
}


/* ---------- Import ---------- */
static void importFolder(const char* path)
{
    DIR* dir = opendir(path);
    if (!dir) return;

    std::vector<std::string> mp3Files;

    struct dirent* ent;
    while ((ent = readdir(dir)))
    {
        if (isMp3(ent->d_name))
            mp3Files.emplace_back(ent->d_name);
    }
    closedir(dir);

    std::sort(mp3Files.begin(), mp3Files.end());

    // ensure clean state before bulk add
    playlistClear();
    mp3ClearMetadata();

    playlistScroll = 0;

    for (auto& f : mp3Files)
    {
        std::string fullPath = std::string(path) + "/" + f;
        if (fullPath.length() >= FB_PATH_LEN)
            fullPath.resize(FB_PATH_LEN - 1);

        mp3Load(fullPath.c_str()); // adds BOTH path and metadata
    }
}



static void importFile(const char* path)
{
    mp3Load(path); // add metadata to playlist
}

/* ---------- Public API ---------- */

void fileBrowserOpen()
{
    playlistClear();
    mp3ClearMetadata();
    scanDirectory("sdmc:/");
    playlistScroll = 0;
    active = true;
    openCooldown = 10; // frames

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

    // --- Navigation ---
    if (down & HidNpadButton_Up)
        selected = (selected > 0) ? selected - 1 : selected;

    if (down & HidNpadButton_Down)
        selected = (selected < itemCount - 1) ? selected + 1 : selected;

    // --- Close file browser ---
    if (down & HidNpadButton_X)
    {
        active = false;
        return;
    }

    // --- Navigate into folders / go up ---
    if (down & HidNpadButton_A)
    {
        BrowserItem* it = &items[selected];

        // Go up
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

        // Enter folder
        if (it->isDir)
        {
            scanDirectory(it->fullpath);
        }

        // Single MP3 file = do nothing (just select it)
    }

    // --- Add to playlist ---
    if (down & HidNpadButton_B)
    {
        BrowserItem* it = &items[selected];

        // ".." = import entire current folder
        if (strcmp(it->name, "..") == 0)
        {
            importFolder(currentPath);
            active = false; // close browser after importing folder
        }
        // MP3 file = import just this file
        else if (!it->isDir && isMp3(it->name))
        {
            importFile(it->fullpath);
        }

        return;
    }
}


/* ---------- Render ---------- */

void fileBrowserRender(SDL_Renderer* renderer, TTF_Font* font)
{
    if (!active) return;

    SDL_Rect bg = { 0, 0, 1920, 1080 };
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 220);
    SDL_RenderFillRect(renderer, &bg);

    SDL_Color white = {255,255,255,255};
    SDL_Color sel   = {255,200,0,255};

    for (int i = 0; i < itemCount; i++)
    {
        SDL_Surface* s = TTF_RenderText_Solid(font, items[i].name, (i == selected) ? sel : white);
        if (!s) continue;

        SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
        SDL_Rect dst = { 220, 130 + i * 30, s->w, s->h };
        SDL_RenderCopy(renderer, t, NULL, &dst);

        SDL_DestroyTexture(t);
        SDL_FreeSurface(s);
    }
}
