#include "filebrowser.h"
#include "playlist.h"
#include "mp3.h"
#include "flac.h"
#include "ogg.h"
#include "wav.h"
#include "ui.h"

#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_set>

/* ============================================================
   CONSTANTS
   The physical screen is 1920×1080 but the entire UI is drawn
   rotated 90° clockwise. A "column" in screen-space is a
   horizontal band in framebuffer-space.

   Column layout (in framebuffer X = screen Y after rotation):
     Each item column is ITEM_W wide.
     The sidebar (Cancel / Done) is SIDEBAR_W wide.
   All Y values are in framebuffer coordinates.
============================================================ */
#define SCREEN_W       1920
#define SCREEN_H       1080
#define FB_PATH_LEN    512
#define MAX_ITEMS      256   // max files/dirs in one directory
#define VISIBLE_COLS   6     // how many item columns fit on screen at once

// Sidebar (leftmost column in framebuffer = bottom strip in rotated view)
#define SIDEBAR_X      520
#define SIDEBAR_W      100
#define SIDEBAR_H      SCREEN_H

// Item columns (everything to the right of the sidebar)
#define ITEMS_START_X  (SIDEBAR_X + SIDEBAR_W)
#define ITEM_W         160   // width of one column in framebuffer
#define ITEM_H         SCREEN_H

// Folder icon strip at the top of each column (in rotated view = left side)
#define ICON_ZONE_H    120   // height of icon+name area
// Add/check button at the bottom of each column (in rotated view = right side)
#define BTN_SIZE       80

/* ============================================================
   COLOURS
============================================================ */
static const SDL_Color COL_BG           = {  10,  20,  10, 245 };
static const SDL_Color COL_SIDEBAR_BG   = {   5,  40,   5, 255 };
static const SDL_Color COL_ITEM_BG      = {  15,  30,  15, 220 };
static const SDL_Color COL_ITEM_SEL_BG  = {   0,  80,   0, 255 };
static const SDL_Color COL_ITEM_SEL2_BG = {   0, 120,   0, 255 }; // darker-green selected (menu)
static const SDL_Color COL_BORDER       = {   0, 180,   0, 255 };
static const SDL_Color COL_TEXT_NORMAL  = { 180, 180, 180, 255 };
static const SDL_Color COL_TEXT_SEL     = {   0, 255,   0, 255 };
static const SDL_Color COL_TEXT_ADDED   = {   0, 255, 100, 255 };
static const SDL_Color COL_FOLDER       = { 220, 170,  50, 255 };
static const SDL_Color COL_BTN_NORMAL   = {  20,  20,  20, 200 };
static const SDL_Color COL_BTN_ADDED    = {   0, 100,  40, 200 };

/* ============================================================
   BROWSER STATE MACHINE
============================================================ */
enum FBScreen
{
    FB_SCREEN_NONE,
    FB_SCREEN_MENU,       // "Add File / Add URL / Cancel"
    FB_SCREEN_BROWSE,     // folder/file browser
};

/* ============================================================
   ITEM
============================================================ */
struct FBItem
{
    char  name[256];
    char  fullpath[FB_PATH_LEN];
    bool  isDir;
    bool  added;    // true after user pressed B/A to add this item
};

/* ============================================================
   STATIC STATE
============================================================ */
static FBScreen   g_screen      = FB_SCREEN_NONE;
static int        g_openCooldown = 0;

// --- Menu screen ---
// 0=Cancel  1=Add URL  2=Add File  3=Add to Playlist
static int        g_menuSel     = 2; // default: "Add File" selected

// --- Browse screen ---
static std::vector<FBItem> g_items;
static int        g_selected    = 0;   // currently highlighted column index
static int        g_scroll      = 0;   // first visible item index
static char       g_currentPath[FB_PATH_LEN] = "sdmc:/";

// Sidebar selection: 0 = Cancel, 1 = Done
static int        g_sidebarSel  = -1;  // -1 = not in sidebar, navigating items
static bool       g_inSidebar   = false;

// Pending additions (paths queued but not yet committed)
static std::unordered_set<std::string> g_pendingFolders;
static std::unordered_set<std::string> g_pendingFiles;

/* ============================================================
   FILE TYPE HELPERS
============================================================ */
static bool isMp3 (const char* n) { const char* e=strrchr(n,'.'); return e&&strcasecmp(e,".mp3") ==0; }
static bool isFlac(const char* n) { const char* e=strrchr(n,'.'); return e&&strcasecmp(e,".flac")==0; }
static bool isOgg (const char* n) { const char* e=strrchr(n,'.'); return e&&strcasecmp(e,".ogg") ==0; }
static bool isWav (const char* n) { const char* e=strrchr(n,'.'); return e&&strcasecmp(e,".wav") ==0; }
static bool isAudioFile(const char* n) { return isMp3(n)||isFlac(n)||isOgg(n)||isWav(n); }

static const char* formatTag(const char* name)
{
    if (isMp3(name))  return "[MP3]";
    if (isFlac(name)) return "[FLAC]";
    if (isOgg(name))  return "[OGG]";
    if (isWav(name))  return "[WAV]";
    return "";
}

/* ============================================================
   IMPORT HELPERS
============================================================ */
static void commitFile(const char* path)
{
    if      (isFlac(path)) flacAddToPlaylist(path);
    else if (isOgg(path))  oggAddToPlaylist(path);
    else if (isWav(path))  wavAddToPlaylist(path);
    else                   mp3AddToPlaylist(path);
}

static void commitFolder(const char* path)
{
    DIR* dir = opendir(path);
    if (!dir) return;

    printf("[FB] importFolder: %s\n", path);

    std::vector<std::string> mp3f, flacf, oggf, wavf;
    struct dirent* ent;
    while ((ent = readdir(dir)))
    {
        if (isMp3(ent->d_name))       mp3f.emplace_back(ent->d_name);
        else if (isFlac(ent->d_name)) flacf.emplace_back(ent->d_name);
        else if (isOgg(ent->d_name))  oggf.emplace_back(ent->d_name);
        else if (isWav(ent->d_name))  wavf.emplace_back(ent->d_name);
    }
    closedir(dir);

    if (mp3f.empty() && flacf.empty() && oggf.empty() && wavf.empty()) return;

    std::vector<std::string> all;
    for (auto& f : mp3f)  all.push_back(f);
    for (auto& f : flacf) all.push_back(f);
    for (auto& f : oggf)  all.push_back(f);
    for (auto& f : wavf)  all.push_back(f);
    std::sort(all.begin(), all.end());

    for (auto& f : all)
        commitFile((std::string(path) + "/" + f).c_str());

    mp3SetLoadedFolder(path);
    flacSetLoadedFolder(path);
    oggSetLoadedFolder(path);
    wavSetLoadedFolder(path);
}

// Commit everything the user marked, then close
static void commitAll()
{
    // Load caches once before adding
    if (!g_pendingFolders.empty() || !g_pendingFiles.empty())
    {
        mp3CancelAllScans();  flacCancelAllScans();
        oggCancelAllScans();  wavCancelAllScans();

        // Only clear playlist if we actually have things to add
        playlistClear();
        mp3ClearMetadata();  flacClearMetadata();
        oggClearMetadata();  wavClearMetadata();

        // Load caches for all pending folders
        for (auto& folder : g_pendingFolders)
        {
            char key[512]; snprintf(key, sizeof(key), "%s/", folder.c_str());
            mp3LoadCache(key);  flacLoadCache(key);
            oggLoadCache(key);  wavLoadCache(key);
        }

        for (auto& folder : g_pendingFolders)
            commitFolder(folder.c_str());

        for (auto& file : g_pendingFiles)
            commitFile(file.c_str());

        playlistScroll = 0;
    }

    g_pendingFolders.clear();
    g_pendingFiles.clear();
    g_screen = FB_SCREEN_NONE;
}

static void cancelAll()
{
    g_pendingFolders.clear();
    g_pendingFiles.clear();
    g_screen = FB_SCREEN_NONE;
}

/* ============================================================
   DIRECTORY SCAN
============================================================ */
static void scanDirectory(const char* path)
{
    g_items.clear();
    g_selected = 0;
    g_scroll   = 0;
    g_inSidebar = false;
    strlcpy(g_currentPath, path, sizeof(g_currentPath));

    DIR* dir = opendir(path);
    if (!dir) return;

    // ".." entry unless at root
    if (strcmp(path, "sdmc:/") != 0)
    {
        FBItem up{};
        strlcpy(up.name,     "..",  sizeof(up.name));
        strlcpy(up.fullpath, path,  sizeof(up.fullpath));
        up.isDir = true;
        // Mark ".." as added if this entire folder is pending
        up.added = (g_pendingFolders.count(path) > 0);
        g_items.push_back(up);
    }

    struct dirent* ent;
    std::vector<FBItem> dirs, files;

    while ((ent = readdir(dir)))
    {
        if (ent->d_name[0] == '.') continue;

        FBItem it{};
        snprintf(it.fullpath, sizeof(it.fullpath), "%s/%s", path, ent->d_name);
        strlcpy(it.name, ent->d_name, sizeof(it.name));

        struct stat st;
        it.isDir = (stat(it.fullpath, &st) == 0) && S_ISDIR(st.st_mode);

        // Mark as added if already in pending set
        if (it.isDir)
            it.added = (g_pendingFolders.count(it.fullpath) > 0);
        else
            it.added = (g_pendingFiles.count(it.fullpath) > 0);

        if (it.isDir)
            dirs.push_back(it);
        else if (isAudioFile(it.name))
            files.push_back(it);
    }
    closedir(dir);

    // Sort dirs and files separately, dirs first
    std::sort(dirs.begin(),  dirs.end(),  [](const FBItem& a, const FBItem& b){ return strcmp(a.name, b.name) < 0; });
    std::sort(files.begin(), files.end(), [](const FBItem& a, const FBItem& b){ return strcmp(a.name, b.name) < 0; });

    for (auto& d : dirs)  g_items.push_back(d);
    for (auto& f : files) g_items.push_back(f);
}

/* ============================================================
   PUBLIC API
============================================================ */
void fileBrowserOpen()
{
    g_menuSel    = 2; // highlight "Add File"
    g_screen     = FB_SCREEN_MENU;
    g_openCooldown = 10;
    g_pendingFolders.clear();
    g_pendingFiles.clear();
}

bool fileBrowserIsActive()
{
    return g_screen != FB_SCREEN_NONE;
}

/* ============================================================
   INPUT
============================================================ */
void fileBrowserUpdate(PadState* pad)
{
    if (g_openCooldown > 0) { g_openCooldown--; return; }
    if (g_screen == FB_SCREEN_NONE) return;

    u64 down = padGetButtonsDown(pad);

    /* --------------------------------------------------------
       MENU SCREEN
    -------------------------------------------------------- */
    if (g_screen == FB_SCREEN_MENU)
    {
        // Left/Right to move between menu items (which are vertical columns)
        if (down & HidNpadButton_Right)
            g_menuSel = (g_menuSel < 3) ? g_menuSel + 1 : 3;
        if (down & HidNpadButton_Left)
            g_menuSel = (g_menuSel > 0) ? g_menuSel - 1 : 0;

        if (down & HidNpadButton_A)
        {
            switch (g_menuSel)
            {
                case 0: // Cancel
                    cancelAll();
                    break;
                case 1: // Add URL — placeholder, not implemented
                    break;
                case 2: // Add File → open browser
                    scanDirectory("sdmc:/");
                    g_screen = FB_SCREEN_BROWSE;
                    g_openCooldown = 6;
                    break;
                case 3: // Add to Playlist — same as Add File for now
                    scanDirectory("sdmc:/");
                    g_screen = FB_SCREEN_BROWSE;
                    g_openCooldown = 6;
                    break;
            }
        }

        // B = cancel/close
        if (down & HidNpadButton_B)
            cancelAll();

        return;
    }

    /* --------------------------------------------------------
       BROWSE SCREEN
    -------------------------------------------------------- */
    if (g_screen == FB_SCREEN_BROWSE)
    {
        int itemCount = (int)g_items.size();

        if (g_inSidebar)
        {
            // In sidebar: Left/Right selects Cancel vs Done
            if (down & HidNpadButton_Right) g_sidebarSel = 1; // Done
            if (down & HidNpadButton_Left)  g_sidebarSel = 0; // Cancel

            // Move right back into item columns
            if (down & HidNpadButton_Up)
            {
                g_inSidebar = false;
            }

            if (down & HidNpadButton_A)
            {
                if (g_sidebarSel == 1)
                    commitAll();
                else
                    cancelAll();
            }

            if (down & HidNpadButton_B)
                cancelAll();

            return;
        }

        // --- Navigating item columns ---

        // Left/Right = scroll through items
        if (down & HidNpadButton_Right)
        {
            if (g_selected < itemCount - 1)
            {
                g_selected++;
                // Advance scroll window if needed
                if (g_selected >= g_scroll + VISIBLE_COLS)
                    g_scroll = g_selected - VISIBLE_COLS + 1;
            }
        }
        if (down & HidNpadButton_Left)
        {
            if (g_selected > 0)
            {
                g_selected--;
                if (g_selected < g_scroll)
                    g_scroll = g_selected;
            }
            else
            {
                // At leftmost item → move into sidebar
                g_inSidebar  = true;
                g_sidebarSel = 1; // default: Done
            }
        }

        // A = enter directory
        if (down & HidNpadButton_A)
        {
            if (itemCount == 0) return;
            FBItem& it = g_items[g_selected];

            if (strcmp(it.name, "..") == 0)
            {
                // Go up one level
                char temp[FB_PATH_LEN];
                snprintf(temp, sizeof(temp), "%s", g_currentPath);
                char* slash = strrchr(temp, '/');
                if (slash && slash != temp)
                {
                    *slash = '\0';
                    scanDirectory(temp);
                }
                else
                {
                    // Already at root, go back to menu
                    g_screen = FB_SCREEN_MENU;
                }
                return;
            }

            if (it.isDir)
            {
                scanDirectory(it.fullpath);
                return;
            }

            // Audio file → mark as added (toggle)
            if (isAudioFile(it.name))
            {
                if (it.added)
                {
                    it.added = false;
                    g_pendingFiles.erase(it.fullpath);
                }
                else
                {
                    it.added = true;
                    g_pendingFiles.insert(it.fullpath);
                }
            }
        }

        // B = add entire current folder (on ".." item) or go up
        if (down & HidNpadButton_B)
        {
            if (itemCount > 0)
            {
                FBItem& it = g_items[g_selected];

                if (strcmp(it.name, "..") == 0 || it.isDir)
                {
                    // Toggle the folder
                    const char* folderPath = (strcmp(it.name, "..") == 0)
                                             ? g_currentPath
                                             : it.fullpath;

                    if (g_pendingFolders.count(folderPath))
                    {
                        g_pendingFolders.erase(folderPath);
                        it.added = false;
                    }
                    else
                    {
                        g_pendingFolders.insert(folderPath);
                        it.added = true;
                    }
                    // Re-scan to update the ".." added state if needed
                    // (just refresh the ".." item's added flag)
                    if (!g_items.empty() && strcmp(g_items[0].name, "..") == 0)
                        g_items[0].added = (g_pendingFolders.count(g_currentPath) > 0);
                }
            }
        }

        // X = go back to menu
        if (down & HidNpadButton_X)
            g_screen = FB_SCREEN_MENU;

        return;
    }
}

/* ============================================================
   RENDER HELPERS
============================================================ */

// Draw a filled rect
static void fb_fillRect(SDL_Renderer* r,
                        int x, int y, int w, int h,
                        SDL_Color c)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc = {x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

// Draw a rect border
static void fb_drawBorder(SDL_Renderer* r,
                          int x, int y, int w, int h,
                          SDL_Color c, int thickness = 2)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    for (int i = 0; i < thickness; i++)
    {
        SDL_Rect rc = {x+i, y+i, w-i*2, h-i*2};
        SDL_RenderDrawRect(r, &rc);
    }
}

// Draw text rotated 90° CW inside a column.
// colX      = left edge of the column in framebuffer coords
// colW      = width of the column in framebuffer coords
// textY     = where the top of the rendered text should appear
//             in framebuffer Y (= position along the column in screen-space)
// SDL_RenderCopyEx with angle=90 rotates the texture clockwise around
// the pivot point. With pivot={0,0} at dst top-left, the top-left of the
// texture maps to the top-left of dst, and the texture grows downward by
// its width (after rotation). We compensate by shifting dst so the visible
// result is centered in the column and starts at textY.
static void fb_drawText(SDL_Renderer* renderer,
                        TTF_Font* font,
                        const char* text,
                        int colX, int colW,
                        int textY,
                        SDL_Color color)
{
    if (!font || !text || !text[0]) return;

    SDL_Surface* s = TTF_RenderUTF8_Solid(font, text, color);
    if (!s) return;
    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
    if (!t) { SDL_FreeSurface(s); return; }

    int tw = s->w;  // texture width  (unrotated)
    int th = s->h;  // texture height (unrotated)
    SDL_FreeSurface(s);

    // After 90° CW rotation around dst top-left {0,0}:
    //   The texture's right edge moves to the bottom.
    //   The rendered region occupies:
    //     horizontal: [dst.x .. dst.x + th]   (th becomes the horizontal extent)
    //     vertical:   [dst.y .. dst.y + tw]    (tw becomes the vertical extent)
    //
    // We want:
    //   horizontal center of text = colX + colW/2
    //   top of rendered text      = textY
    //
    // So:
    //   dst.x + th/2 = colX + colW/2  →  dst.x = colX + (colW - th) / 2
    //   dst.y        = textY

    SDL_Rect dst;
    dst.x = colX + (colW - th) / 2;
    dst.y = textY;
    dst.w = tw;
    dst.h = th;

    SDL_Point pivot = {0, 0};
    SDL_RenderCopyEx(renderer, t, NULL, &dst, 90.0, &pivot, SDL_FLIP_NONE);

    SDL_DestroyTexture(t);
}

// Draw a folder icon that appears upright on the rotated screen.
// In framebuffer coords, "up" on screen = left in framebuffer.
// So we draw the icon as if X is vertical and Y is horizontal.
// colX/colW define the column, iconY is the framebuffer-Y start (= screen left).
static void fb_drawFolderIcon(SDL_Renderer* renderer,
                               int colX, int colW,
                               int iconY)
{
    // Centre the icon horizontally within the column
    int cx = colX + colW / 2;

    // In framebuffer space (before rotation perception):
    // The folder needs to look upright after 90° CW rotation.
    // We draw it rotated 90° CCW so it appears correct on screen.
    // Simplest approach: render as an SDL_Texture via surface then RenderCopyEx.

    // Build a small software surface for the icon (56x52 pixels)
    const int IW = 56;
    const int IH = 52;
    SDL_Surface* surf = SDL_CreateRGBSurface(0, IW, IH, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surf) return;

    SDL_FillRect(surf, NULL, SDL_MapRGBA(surf->format, 0, 0, 0, 0));

    // Tab (top-left of folder body in icon space)
    SDL_Rect tab  = {0, 0, 30, 10};
    SDL_FillRect(surf, &tab,
                 SDL_MapRGB(surf->format, 200, 150, 30));

    // Body
    SDL_Rect body = {0, 8, IW, IH - 8};
    SDL_FillRect(surf, &body,
                 SDL_MapRGB(surf->format, 220, 170, 50));

    // Shine
    SDL_Rect shine = {6, 13, 22, 8};
    SDL_FillRect(surf, &shine,
                 SDL_MapRGBA(surf->format, 255, 220, 120, 180));

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;

    // Place icon centered in column, at iconY along the column.
    // After 90° CW rotation around pivot {0,0}:
    //   horizontal extent = IH, vertical extent = IW
    // Center horizontally: dst.x = cx - IH/2
    // Top of rendered icon at iconY: dst.y = iconY
    SDL_Rect dst = { cx - IH / 2, iconY, IW, IH };
    SDL_Point pivot = {0, 0};
    SDL_RenderCopyEx(renderer, tex, NULL, &dst, 90.0, &pivot, SDL_FLIP_NONE);

    SDL_DestroyTexture(tex);
}

// Draw a checkmark using line primitives, oriented correctly for 90° CW rotated screen.
// cx, cy are framebuffer coordinates (center of the button box).
// The lines are drawn in framebuffer space such that after 90° CW screen rotation
// the checkmark appears upright: short stroke down-left, long stroke up-right.
static void fb_drawCheckmark(SDL_Renderer* renderer,
                              int cx, int cy, int size, SDL_Color col)
{
    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
    int s = size / 2;
    // On the rotated screen, framebuffer-X maps to screen-Y (vertical) and
    // framebuffer-Y maps to screen-X (horizontal).
    // To draw a checkmark that looks correct after CW rotation, we draw it
    // as if rotated 90° CCW in framebuffer space:
    //   Short left stroke:  goes right+up in framebuffer → down-left on screen
    //   Long right stroke:  goes right+down in framebuffer → up-right on screen (the long tick)
    for (int t = -1; t <= 1; t++)
    {
        // Short stroke: from left to middle-low (in screen space: bottom-left of tick)
        SDL_RenderDrawLine(renderer,
                           cx - s,         cy + t,
                           cx - s/4,       cy + s/2 + t);
        // Long stroke: from middle-low to right-high (the long upward tick)
        SDL_RenderDrawLine(renderer,
                           cx - s/4,       cy + s/2 + t,
                           cx + s,         cy - s + t);
    }
}

// Draw a + using line primitives, thick and centered.
static void fb_drawPlus(SDL_Renderer* renderer,
                         int cx, int cy, int size, SDL_Color col)
{
    SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
    int h = size / 2;
    for (int t = -2; t <= 2; t++)
    {
        SDL_RenderDrawLine(renderer, cx - h, cy + t, cx + h, cy + t); // horizontal
        SDL_RenderDrawLine(renderer, cx + t, cy - h, cx + t, cy + h); // vertical
    }
}

// Draw the +/✓ button square at the bottom of a column.
// Scroll arrows (< >) are drawn outside the box, below it.
static void fb_drawAddButton(SDL_Renderer* renderer,
                              TTF_Font* font,
                              int colX, int colW,
                              bool added, bool selected,
                              bool showLeft, bool showRight)
{
    const int BOX = 80;
    int btnY = SCREEN_H - BOX - 8;
    int btnX = colX + (colW - BOX) / 2;

    SDL_Color bgColor = added    ? COL_BTN_ADDED
                      : selected ? SDL_Color{0, 60, 0, 220}
                      :            COL_BTN_NORMAL;
    fb_fillRect(renderer, btnX, btnY, BOX, BOX, bgColor);
    fb_drawBorder(renderer, btnX, btnY, BOX, BOX, COL_BORDER, 2);

    // Symbol centered in the box
    int cx = btnX + BOX / 2;
    int cy = btnY + BOX / 2;
    SDL_Color symColor = added ? COL_TEXT_ADDED : COL_TEXT_SEL;

    if (added)
        fb_drawCheckmark(renderer, cx, cy, 28, symColor);
    else
        fb_drawPlus(renderer, cx, cy, 28, symColor);

    // Scroll arrows drawn ABOVE the button box in screen space.
    // "Above" on screen = lower framebuffer Y = smaller btnY value.
    // Place them just above the box, centered in the column.
    if (showLeft)
    {
        fb_drawText(renderer, font, "<",
                    colX, colW,
                    btnY - 36,
                    COL_TEXT_SEL);
    }
    if (showRight)
    {
        fb_drawText(renderer, font, ">",
                    colX, colW,
                    btnY - 36,
                    COL_TEXT_SEL);
    }
}

/* ============================================================
   RENDER
============================================================ */
void fileBrowserRender(SDL_Renderer* renderer, TTF_Font* font)
{
    if (g_screen == FB_SCREEN_NONE) return;

    /* ---- Full screen dark background ---- */
    fb_fillRect(renderer, 0, 0, SCREEN_W, SCREEN_H, COL_BG);

    /* ====================================================
       SCREEN 1: MENU
    ==================================================== */
    if (g_screen == FB_SCREEN_MENU)
    {
        struct { const char* label; } menuItems[4] = {
            {"Cancel"},
            {"Add URL"},
            {"Add File"},
            {"Add to Playlist"}
        };
        const int numItems = 4;
        const int MARGIN   = 60;   // gap from screen edges
        const int GAP      = 12;   // gap between buttons
        const int BTN_Y    = MARGIN;
        const int BTN_H    = SCREEN_H - MARGIN * 2;
        const int totalW   = SCREEN_W - MARGIN * 2;
        const int btnW     = (totalW - GAP * (numItems - 1)) / numItems;

        for (int i = 0; i < numItems; i++)
        {
            int btnX = MARGIN + i * (btnW + GAP);
            bool sel = (i == g_menuSel);

            SDL_Color bg = sel
                ? SDL_Color{0, 100, 0, 255}
                : SDL_Color{10, 25, 10, 220};
            fb_fillRect(renderer, btnX, BTN_Y, btnW, BTN_H, bg);

            SDL_Color border = sel ? COL_BORDER : SDL_Color{0, 80, 0, 200};
            fb_drawBorder(renderer, btnX, BTN_Y, btnW, BTN_H, border, 3);

            // Center label near the top of each button column
            SDL_Color textColor = sel ? COL_TEXT_SEL : COL_TEXT_NORMAL;
            fb_drawText(renderer, font, menuItems[i].label,
                        btnX, btnW,
                        BTN_Y + 30,
                        textColor);
        }

        // Instruction hint — tight strip on the right edge of framebuffer
        // (= bottom of rotated screen, below the buttons)
        {
            const int HDR_W = 160;
            int hdrX = SCREEN_W - HDR_W - 6;
            fb_fillRect(renderer, hdrX, MARGIN, HDR_W, BTN_H,
                        SDL_Color{5, 30, 5, 220});
            fb_drawBorder(renderer, hdrX, MARGIN, HDR_W, BTN_H,
                          COL_BORDER, 2);
            fb_drawText(renderer, font, "A: Select   B: Cancel",
                        hdrX, HDR_W,
                        MARGIN + 20,
                        COL_TEXT_NORMAL);
        }
        return;
    }

    /* ====================================================
       SCREEN 2: BROWSE
    ==================================================== */
    if (g_screen == FB_SCREEN_BROWSE)
    {
        int itemCount = (int)g_items.size();

        // ---- Layout constants for this screen ----
        // The sidebar sits at SIDEBAR_X and is SIDEBAR_W wide.
        // We split it into three zones (framebuffer Y axis = screen X axis):
        //   [0 .. CANCEL_H)            : Cancel button
        //   [CANCEL_H .. CANCEL_H+DONE_H) : Done button
        //   [CANCEL_H+DONE_H .. SCREEN_H) : Instruction strip
        const int CANCEL_H = 360;   // height of Cancel box in framebuffer
        const int DONE_H   = 360;   // height of Done box in framebuffer
        const int PAD      = 6;     // padding between boxes

        // ---- "// SELECT FILES" header ----
        // Drawn right-aligned in screen space = at high framebuffer X.
        // We put it in a narrow strip on the RIGHT edge of the framebuffer
        // (= bottom of screen after rotation), tight border around just the text.
        {
            const int HDR_W   = 180;
            const int HDR_H   = SCREEN_H - 20;
            int hdrX = SCREEN_W - HDR_W - 6;

            fb_fillRect(renderer, hdrX, 10, HDR_W, HDR_H,
                        SDL_Color{5, 30, 5, 220});
            fb_drawBorder(renderer, hdrX, 10, HDR_W, HDR_H,
                          COL_BORDER, 2);

            fb_drawText(renderer, font, "// SELECT FILES",
                        hdrX, HDR_W,
                        20,
                        COL_TEXT_SEL);
        }

        // ---- Sidebar background (no outer border — boxes have their own) ----
        fb_fillRect(renderer, SIDEBAR_X, 0, SIDEBAR_W, SCREEN_H, COL_SIDEBAR_BG);

        // ---- Cancel box ----
        {
            bool sel = g_inSidebar && g_sidebarSel == 0;
            SDL_Color bg = sel ? COL_ITEM_SEL_BG : COL_ITEM_BG;
            int boxY = PAD;
            int boxH = CANCEL_H - PAD * 2;
            fb_fillRect(renderer,  SIDEBAR_X + PAD, boxY, SIDEBAR_W - PAD*2, boxH, bg);
            fb_drawBorder(renderer, SIDEBAR_X + PAD, boxY, SIDEBAR_W - PAD*2, boxH,
                          COL_BORDER, sel ? 3 : 1);
            // Center text: textY = boxY + (boxH - fontHeight) / 2
            // fb_drawText with textY positions the start of the rendered (rotated) text.
            // After rotation, the text width becomes the vertical extent.
            // Approximate center: boxY + boxH/2 - estimatedTextWidth/2.
            // For a ~32px font "Cancel" ≈ 6 chars × 18px = ~108px wide.
            // Center: boxY + (boxH - 108) / 2
            int textCenter = boxY + (boxH - 110) / 2;
            fb_drawText(renderer, font, "Cancel",
                        SIDEBAR_X + PAD, SIDEBAR_W - PAD*2,
                        textCenter,
                        sel ? COL_TEXT_SEL : COL_TEXT_NORMAL);
        }

        // ---- Done box ----
        {
            bool sel = g_inSidebar && g_sidebarSel == 1;
            SDL_Color bg = sel ? COL_ITEM_SEL_BG : COL_ITEM_BG;
            int boxY = CANCEL_H + PAD;
            int boxH = DONE_H - PAD * 2;
            fb_fillRect(renderer,  SIDEBAR_X + PAD, boxY, SIDEBAR_W - PAD*2, boxH, bg);
            fb_drawBorder(renderer, SIDEBAR_X + PAD, boxY, SIDEBAR_W - PAD*2, boxH,
                          COL_BORDER, sel ? 3 : 1);
            // "Done" ≈ 4 chars × 18px = ~72px. Center: boxY + (boxH - 72) / 2
            int textCenter = boxY + (boxH - 75) / 2;
            fb_drawText(renderer, font, "Done",
                        SIDEBAR_X + PAD, SIDEBAR_W - PAD*2,
                        textCenter,
                        sel ? COL_TEXT_SEL : SDL_Color{0, 180, 0, 200});
        }

        // ---- Instruction strip (below Done, to bottom of sidebar) ----
        {
            int stripY = CANCEL_H + DONE_H + PAD;
            int stripH = SCREEN_H - stripY - PAD;
            fb_fillRect(renderer,  SIDEBAR_X + PAD, stripY, SIDEBAR_W - PAD*2, stripH,
                        SDL_Color{8, 25, 8, 200});
            fb_drawBorder(renderer, SIDEBAR_X + PAD, stripY, SIDEBAR_W - PAD*2, stripH,
                          SDL_Color{0, 100, 0, 180}, 1);
            fb_drawText(renderer, font,
                        "A:Enter  B:Folder  X:Menu  Left:Sidebar",
                        SIDEBAR_X + PAD, SIDEBAR_W - PAD*2,
                        stripY + 10,
                        COL_TEXT_NORMAL);
        }

        /* ---- Item columns ---- */
        int visible = std::min(VISIBLE_COLS, itemCount - g_scroll);

        // Columns occupy from ITEMS_START_X to SCREEN_W - HDR_W - 6 - some margin
        const int HDR_STRIP_W = 192; // matches hdrX calc above
        int itemsEndX  = SCREEN_W - HDR_STRIP_W - 10;
        int effectiveItemW = (itemsEndX - ITEMS_START_X) / VISIBLE_COLS;
        if (effectiveItemW < 1) effectiveItemW = ITEM_W;

        for (int vi = 0; vi < visible; vi++)
        {
            int idx    = g_scroll + vi;
            FBItem& it = g_items[idx];
            bool isSel = (!g_inSidebar && idx == g_selected);

            int colX = ITEMS_START_X + vi * effectiveItemW;
            int colW = effectiveItemW;

            // Column background
            SDL_Color bg = isSel ? COL_ITEM_SEL_BG : COL_ITEM_BG;
            fb_fillRect(renderer, colX, 0, colW, SCREEN_H, bg);

            // Column border
            SDL_Color border = isSel ? COL_BORDER : SDL_Color{0, 60, 0, 180};
            fb_drawBorder(renderer, colX, 0, colW, SCREEN_H, border,
                          isSel ? 3 : 1);

            // Icon area
            if (it.isDir)
            {
                fb_drawFolderIcon(renderer, colX, colW, 14);
            }
            else
            {
                SDL_Color fmtColor = {100, 100, 100, 200};
                if (isMp3(it.name))  fmtColor = { 80, 160,  80, 220};
                if (isFlac(it.name)) fmtColor = { 60, 120, 200, 220};
                if (isOgg(it.name))  fmtColor = {160,  80, 160, 220};
                if (isWav(it.name))  fmtColor = {160, 140,  60, 220};
                fb_fillRect(renderer, colX + 6, 6, colW - 12, 16, fmtColor);
            }

            // Name label
            SDL_Color textColor = isSel    ? COL_TEXT_SEL
                                : it.isDir ? COL_FOLDER
                                :            COL_TEXT_NORMAL;

            char displayName[300];
            if (!it.isDir && isAudioFile(it.name))
                snprintf(displayName, sizeof(displayName),
                         "%s %s", it.name, formatTag(it.name));
            else
                snprintf(displayName, sizeof(displayName), "%s", it.name);

            fb_drawText(renderer, font, displayName,
                        colX, colW,
                        ICON_ZONE_H,
                        textColor);

            // Scroll arrows outside the + box
            bool arrowLeft  = (vi == 0)            && (g_scroll > 0);
            bool arrowRight = (vi == visible - 1)  && (g_scroll + VISIBLE_COLS < itemCount);

            fb_drawAddButton(renderer, font, colX, colW,
                             it.added, isSel,
                             arrowLeft, arrowRight);
        }

        return;
    }
}
