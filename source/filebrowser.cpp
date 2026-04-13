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
#include <math.h>

/* ============================================================
   COORDINATE SYSTEM  — verified against ui.cpp rects
   ============================================================
   The Switch is held upright (portrait). The framebuffer is 1920×1080
   landscape, but everything is drawn rotated 90° CW so it looks
   portrait on the physical screen.

   Framebuffer rect {x, y, w, h}:
     x        = distance from LEFT of framebuffer
              = distance from TOP of the portrait screen
     x + w    = distance to RIGHT of framebuffer
              = distance to BOTTOM of portrait screen
     y        = distance from TOP of framebuffer
              = distance from LEFT of portrait screen
     y + h    = distance to BOTTOM of framebuffer
              = distance to RIGHT of portrait screen

   So when laying out portrait UI rows (top → bottom of screen):
     Use increasing FRAMEBUFFER X values.
     Screen width (left→right) = framebuffer height = 1080 px.
     Screen height (top→bottom) = framebuffer width = 1920 px.

   drawVerticalText(renderer, font, text, rect, color, paddingX, paddingY, align):
     rect.x = where in FB-X (= screen Y) the band starts
     rect.w = thickness of the band in FB-X (= height of row on screen)
     rect.y = FB-Y left edge of text area  (= screen X left edge)
     rect.h = FB-Y width of text area      (= screen width available for text)
     paddingX offsets from the right side of the rect (inside the band)
     ALIGN_CENTER centres text in the rect.h range

   drawRect fills a framebuffer rect directly (no rotation).

   ============================================================
   LAYOUT CONSTANTS — all in framebuffer coordinates
   Screen top    = FB X = 0
   Screen bottom = FB X = 1920
   Screen left   = FB Y = 0
   Screen right  = FB Y = 1080
   ============================================================ */

// Full screen dimensions in framebuffer space
#define FBW   1920   // framebuffer width  = screen height (portrait)
#define FBH   1080   // framebuffer height = screen width  (portrait)

// ---- MENU SCREEN layout (FB X values = screen Y positions) ----
#define MENU_MARGIN_TOP   800   // blank space before first button
#define MENU_ROW_H        130   // height of each button row on screen
#define MENU_TITLE_H       80   // height of title row
#define MENU_GAP           12   // gap between rows
// Y-span for text inside rows (full width with padding)
#define MENU_TXT_Y         20
#define MENU_TXT_H        (FBH - 30)

// ---- BROWSE SCREEN layout (FB X values = screen Y positions) ----
#define BR_MARGIN_TOP    400   // blank space at top
#define BR_HDR_H          80   // "// SELECT FILES [<][>]" header
#define BR_ROW_H         140   // each file/folder row
#define BR_ROWS            6   // visible rows
#define BR_BTNS_H         80   // cancel/done row
#define BR_HINT_H         60   // hint text row
#define BR_GAP            110
// Text area Y/H for full-width rows
#define BR_TXT_Y          20
#define BR_TXT_H         (FBH - 40)
// Add button dimensions (right side of each file row)
#define ADD_BTN_W         70   // button width in FB-Y = screen horizontal size
#define ADD_BTN_H         70   // button height in FB-X = screen vertical size
#define ADD_BTN_MARGIN    10   // gap from right edge

// Page jump for scroll
#define PAGE_JUMP          6

/* ============================================================
   COLOURS
============================================================ */
#define COL_BG         SDL_Color{  8,  16,  8, 255}
#define COL_BLOCK      SDL_Color{ 12,  24, 12, 220}
#define COL_SEL        SDL_Color{  0,  90,  0, 255}
#define COL_TITLE      SDL_Color{  4,  20,  4, 240}
#define COL_BORDER     SDL_Color{  0, 180,  0, 255}
#define COL_BRD_DIM    SDL_Color{  0,  60,  0, 180}
#define COL_WHITE      SDL_Color{255, 255,255, 255}
#define COL_GREEN      SDL_Color{  0, 255,  0, 255}
#define COL_GREEN_DIM  SDL_Color{  0, 180,  0, 200}
#define COL_GREY       SDL_Color{160, 160,160, 255}
#define COL_GREY_DIM   SDL_Color{100, 100,100, 200}
#define COL_FOLDER_CLR SDL_Color{220, 170, 50, 255}
#define COL_ADD_CLR    SDL_Color{  0, 255,120, 255}
#define COL_BTN        SDL_Color{ 15,  15, 15, 200}
#define COL_BTN_DONE   SDL_Color{  0,  80, 35, 200}

/* ============================================================
   STATE
============================================================ */
#define FB_PATH_LEN 512

enum FBScreen { FB_NONE, FB_MENU, FB_BROWSE };

struct FBItem {
    char name    [256];
    char fullpath[FB_PATH_LEN];
    bool isDir;
    bool added;
};

static FBScreen  g_screen   = FB_NONE;
static int       g_cooldown = 0;
static int       g_menuSel  = 0;   // 0=AddFiles 1=AddURL (hint row not selectable)

static std::vector<FBItem>             g_items;
static int                             g_sel    = 0;
static int                             g_scroll = 0;
static char                            g_path[FB_PATH_LEN] = "sdmc:/";

static std::unordered_set<std::string> g_pendFiles;
static std::unordered_set<std::string> g_pendFolders;

/* ============================================================
   FILE TYPE HELPERS
============================================================ */
static bool isMp3 (const char* n){const char* e=strrchr(n,'.');return e&&strcasecmp(e,".mp3" )==0;}
static bool isFlac(const char* n){const char* e=strrchr(n,'.');return e&&strcasecmp(e,".flac")==0;}
static bool isOgg (const char* n){const char* e=strrchr(n,'.');return e&&strcasecmp(e,".ogg" )==0;}
static bool isWav (const char* n){const char* e=strrchr(n,'.');return e&&strcasecmp(e,".wav" )==0;}
static bool isAudio(const char* n){return isMp3(n)||isFlac(n)||isOgg(n)||isWav(n);}
static const char* fmtTag(const char* n){
    if(isMp3(n))  return "[MP3]";
    if(isFlac(n)) return "[FLAC]";
    if(isOgg(n))  return "[OGG]";
    if(isWav(n))  return "[WAV]";
    return "";
}

/* ============================================================
   IMPORT
============================================================ */
static void commitFile(const char* p){
    if(isFlac(p)) flacAddToPlaylist(p);
    else if(isOgg(p))  oggAddToPlaylist(p);
    else if(isWav(p))  wavAddToPlaylist(p);
    else               mp3AddToPlaylist(p);
}
static void commitFolder(const char* path){
    DIR* dir=opendir(path); if(!dir) return;
    printf("[FB] commitFolder: %s\n",path);
    std::vector<std::string> all;
    struct dirent* ent;
    while((ent=readdir(dir))) if(isAudio(ent->d_name)) all.push_back(ent->d_name);
    closedir(dir);
    std::sort(all.begin(),all.end());
    for(auto& f:all) commitFile((std::string(path)+"/"+f).c_str());
    mp3SetLoadedFolder(path); flacSetLoadedFolder(path);
    oggSetLoadedFolder(path); wavSetLoadedFolder(path);
}
static void doCommit(){
    if(g_pendFolders.empty()&&g_pendFiles.empty()){g_screen=FB_NONE;return;}
    mp3CancelAllScans(); flacCancelAllScans(); oggCancelAllScans(); wavCancelAllScans();
    playlistClear();
    mp3ClearMetadata(); flacClearMetadata(); oggClearMetadata(); wavClearMetadata();
    for(auto& f:g_pendFolders){
        char k[512]; snprintf(k,sizeof(k),"%s/",f.c_str());
        mp3LoadCache(k); flacLoadCache(k); oggLoadCache(k); wavLoadCache(k);
    }
    for(auto& f:g_pendFolders) commitFolder(f.c_str());
    for(auto& f:g_pendFiles)   commitFile(f.c_str());
    playlistScroll=0;
    g_pendFolders.clear(); g_pendFiles.clear();
    g_screen=FB_NONE;
}
static void doCancel(){
    g_pendFolders.clear(); g_pendFiles.clear();
    g_screen=FB_NONE;
}

/* ============================================================
   DIRECTORY SCAN
============================================================ */
static void scanDir(const char* path){
    g_items.clear(); g_sel=0; g_scroll=0;
    strlcpy(g_path,path,sizeof(g_path));
    DIR* dir=opendir(path); if(!dir) return;
    if(strcmp(path,"sdmc:/")!=0){
        FBItem up{};
        strlcpy(up.name,"..",sizeof(up.name));
        strlcpy(up.fullpath,path,sizeof(up.fullpath));
        up.isDir=true; up.added=(g_pendFolders.count(path)>0);
        g_items.push_back(up);
    }
    std::vector<FBItem> dirs,files;
    struct dirent* ent;
    while((ent=readdir(dir))){
        if(ent->d_name[0]=='.') continue;
        FBItem it{};
        snprintf(it.fullpath,sizeof(it.fullpath),"%s/%s",path,ent->d_name);
        strlcpy(it.name,ent->d_name,sizeof(it.name));
        struct stat st;
        it.isDir=(stat(it.fullpath,&st)==0)&&S_ISDIR(st.st_mode);
        it.added=it.isDir?(g_pendFolders.count(it.fullpath)>0):(g_pendFiles.count(it.fullpath)>0);
        if(it.isDir) dirs.push_back(it);
        else if(isAudio(it.name)) files.push_back(it);
    }
    closedir(dir);
    auto cmp=[](const FBItem& a,const FBItem& b){return strcmp(a.name,b.name)<0;};
    std::sort(dirs.begin(),dirs.end(),cmp);
    std::sort(files.begin(),files.end(),cmp);
    for(auto& d:dirs)  g_items.push_back(d);
    for(auto& f:files) g_items.push_back(f);
}

/* ============================================================
   PUBLIC API
============================================================ */
void fileBrowserOpen(){
    g_menuSel=0; g_screen=FB_MENU; g_cooldown=10;
    g_pendFiles.clear(); g_pendFolders.clear();
}
bool fileBrowserIsActive(){ return g_screen!=FB_NONE; }

void fileBrowserScrollPage(int dir, int jump){
    if(g_screen!=FB_BROWSE) return;
    int total=(int)g_items.size();
    int step=(jump>0)?jump:PAGE_JUMP;
    g_scroll+=dir*step;
    if(g_scroll<0) g_scroll=0;
    int maxS=std::max(0,total-BR_ROWS);
    if(g_scroll>maxS) g_scroll=maxS;
    if(g_sel<g_scroll) g_sel=g_scroll;
    if(g_sel>=g_scroll+BR_ROWS) g_sel=g_scroll+BR_ROWS-1;
    if(g_sel>=total) g_sel=total-1;
    if(g_sel<0) g_sel=0;
}

/* ============================================================
   INPUT
============================================================ */
void fileBrowserUpdate(PadState* pad){
    if(g_cooldown>0){g_cooldown--;return;}
    if(g_screen==FB_NONE) return;
    u64 dn=padGetButtonsDown(pad);

    if(g_screen==FB_MENU){
        // Screen rows top→bottom: AddFiles(sel=0), AddURL(sel=1), hint(not sel)
        // Up on screen = lower FB X = decrease sel
        if(dn&HidNpadButton_Up)   g_menuSel=(g_menuSel>0)?g_menuSel-1:0;
        if(dn&HidNpadButton_Down) g_menuSel=(g_menuSel<1)?g_menuSel+1:1;
        if(dn&HidNpadButton_A){
            if(g_menuSel==0){scanDir("sdmc:/");g_screen=FB_BROWSE;g_cooldown=6;}
            // menuSel==1 = Add URL: placeholder, do nothing
        }
        if(dn&HidNpadButton_B) doCancel();
        return;
    }

    if(g_screen==FB_BROWSE){
        int total=(int)g_items.size();
        if(dn&HidNpadButton_Up){
            if(g_sel>0){ g_sel--; if(g_sel<g_scroll) g_scroll=g_sel; }
        }
        if(dn&HidNpadButton_Down){
            if(g_sel<total-1){
                g_sel++;
                if(g_sel>=g_scroll+BR_ROWS) g_scroll=g_sel-BR_ROWS+1;
            }
        }
        // Left/Right = page scroll
        if(dn&HidNpadButton_Left)  fileBrowserScrollPage(-1,0);
        if(dn&HidNpadButton_Right) fileBrowserScrollPage(+1,0);
        // A: enter dir / toggle file
        if(dn&HidNpadButton_A){
            if(total==0) return;
            FBItem& it=g_items[g_sel];
            if(strcmp(it.name,"..")==0){
                char tmp[FB_PATH_LEN]; snprintf(tmp,sizeof(tmp),"%s",g_path);
                char* sl=strrchr(tmp,'/');
                if(sl&&sl!=tmp){*sl='\0';scanDir(tmp);}
                else g_screen=FB_MENU;
                return;
            }
            if(it.isDir){scanDir(it.fullpath);return;}
            if(isAudio(it.name)){
                it.added=!it.added;
                if(it.added) g_pendFiles.insert(it.fullpath);
                else         g_pendFiles.erase(it.fullpath);
            }
        }
        // B: toggle folder
        if(dn&HidNpadButton_B){
            if(total==0) return;
            FBItem& it=g_items[g_sel];
            if(strcmp(it.name,"..")==0||it.isDir){
                const char* fp=(strcmp(it.name,"..")==0)?g_path:it.fullpath;
                it.added=!it.added;
                if(it.added) g_pendFolders.insert(fp);
                else         g_pendFolders.erase(fp);
                if(!g_items.empty()&&strcmp(g_items[0].name,"..")==0)
                    g_items[0].added=(g_pendFolders.count(g_path)>0);
            }
        }
        if(dn&HidNpadButton_Y)    doCommit();  // Done
        if(dn&HidNpadButton_X)    g_screen=FB_MENU;
        if(dn&HidNpadButton_Plus) doCancel();
    }
}

/* ============================================================
   DRAW HELPERS
   Use drawVerticalText from ui.h for all text.
   Use drawRect from ui.h for filled rectangles.
   All rect coordinates are in framebuffer space.
============================================================ */

// Draw a bordered rectangle (fill + border).
// rect in framebuffer coords.
static void fbDrawBox(SDL_Renderer* r, SDL_Rect rect,
                      SDL_Color fill, SDL_Color border, int thick=2)
{
    drawRect(r, rect, fill.r, fill.g, fill.b, fill.a);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    for(int i=0;i<thick;i++){
        SDL_Rect br={rect.x+i,rect.y+i,rect.w-i*2,rect.h-i*2};
        SDL_RenderDrawRect(r,&br);
    }
}

// Draw a full-width row: fills from y=0 to y=FBH, x=fbx, w=fbw.
static void fbDrawRow(SDL_Renderer* r, int fbx, int fbw,
                      SDL_Color fill, SDL_Color border, int thick=2)
{
    SDL_Rect rect={fbx, 0, fbw, FBH};
    fbDrawBox(r, rect, fill, border, thick);
}

// Draw text centred vertically in a row, using drawVerticalText.
// fbx = FB X start of row, fbw = row FB width (screen height of row)
// text appears centred in the full screen width
static void fbRowText(SDL_Renderer* r, TTF_Font* font,
                     const char* txt, int fbx, int fbw,
                     SDL_Color col, int offsetY = 0)
{
    SDL_Rect rect = {fbx - offsetY, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, 0, 0, ALIGN_CENTER);
}

// Same but left-aligned (text starts at paddingY from left of screen)
static void fbRowTextLeft(SDL_Renderer* r, TTF_Font* font,
                         const char* txt, int fbx, int fbw,
                         SDL_Color col, int screenLeftPad = 30,
                         int offsetY = 0)
{
    SDL_Rect rect = {fbx - offsetY, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, 0, screenLeftPad, ALIGN_TOP);
}

// Draw text right-aligned (near right side of screen = high FB Y)
static void fbRowTextRight(SDL_Renderer* r, TTF_Font* font,
                            const char* txt, int fbx, int fbw,
                            SDL_Color col, int screenRightPad=40)
{
    SDL_Rect rect={fbx, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, 0, screenRightPad, ALIGN_BOTTOM);
}

// Draw a folder icon surface, centred in the row at the left side.
// fbx = FB X start of row, fbw = row FB width
static void fbFolderIcon(SDL_Renderer* r, int fbx, int fbw)
{
    const int IW=52, IH=44;
    SDL_Surface* surf=SDL_CreateRGBSurface(0,IW,IH,32,
        0x00FF0000,0x0000FF00,0x000000FF,0xFF000000);
    if(!surf) return;
    SDL_FillRect(surf,NULL,SDL_MapRGBA(surf->format,0,0,0,0));
    SDL_Rect tab ={0, 0,24, 9}; SDL_FillRect(surf,&tab, SDL_MapRGB(surf->format,200,150,30));
    SDL_Rect body={0, 7,IW,37}; SDL_FillRect(surf,&body,SDL_MapRGB(surf->format,220,170,50));
    SDL_Rect shine={4,11,18, 6}; SDL_FillRect(surf,&shine,SDL_MapRGBA(surf->format,255,220,120,180));
    SDL_Texture* tex=SDL_CreateTextureFromSurface(r,surf);
    SDL_FreeSurface(surf);
    if(!tex) return;

    // The icon should appear at the left/top of screen = low FB Y,
    // centred vertically in the row = centred in FB X.
    // After 90° CW rotation: IW maps to FB-X extent, IH maps to FB-Y extent.
    // Wait — we want the icon to be NOT rotated (already drawn upright in previous version).
    // Actually the folder icon is rendered using RenderCopyEx(90°) which makes it
    // look correct on the rotated screen. The icon surface is drawn "sideways"
    // in surface space so after rotation it looks upright.
    // Center in row FB-X: dst.x = fbx + (fbw - IH)/2  (IH becomes FB-X after rotation)
    // Left of screen = low FB-Y: dst.y = 20
    //SDL_Rect dst={fbx + (fbw / 2) - (IH / 2),20,IW,IH};
    const int ICON_OFFSET_Y = 50;
    SDL_Rect dst={fbx + (fbw / 2) - (IH / 2) + ICON_OFFSET_Y,20,IW,IH};

    SDL_Point piv={0,0};
    SDL_RenderCopyEx(r,tex,NULL,&dst,90.0,&piv,SDL_FLIP_NONE);
    SDL_DestroyTexture(tex);
}

// Draw a format colour bar at left side of row (where icon would be for audio files)
static void fbFormatBar(SDL_Renderer* r, int fbx, int fbw, const char* name)
{
    SDL_Color fc={80,80,80,180};
    if(isMp3(name))  fc={ 80,160, 80,200};
    if(isFlac(name)) fc={ 60,120,200,200};
    if(isOgg(name))  fc={160, 80,160,200};
    if(isWav(name))  fc={160,140, 60,200};
    // Thin bar at left of screen = low FB Y
    SDL_Rect bar={fbx+4, 4, fbw-8, 14};
    drawRect(r,bar,fc.r,fc.g,fc.b,fc.a);
}

// Draw the +/✓ button on the right side of a row.
// The button is a square at high FB Y (= right side of screen).
// Returns the SDL_Rect of the button (for touch hit-testing later).
static SDL_Rect fbAddBtn(SDL_Renderer* r, int fbx, int fbw, bool added, bool rowSel)
{
    // Position: right side of screen = high FB Y
    // FB Y for button right edge: FBH - ADD_BTN_MARGIN
    // FB Y for button left edge:  FBH - ADD_BTN_MARGIN - ADD_BTN_W
    int bFBY = FBH - ADD_BTN_MARGIN - ADD_BTN_W;
    // Vertically centred in row: FB X = fbx + (fbw - ADD_BTN_H)/2
    int bFBX = fbx + (fbw / 2) - (ADD_BTN_H / 2);

    SDL_Rect btn={bFBX, bFBY, ADD_BTN_H, ADD_BTN_W};

    SDL_Color bg = added    ? COL_BTN_DONE
                 : rowSel   ? SDL_Color{0,50,0,220}
                 :             COL_BTN;
    fbDrawBox(r, btn, bg, COL_BORDER, 2);

    // Draw + or ✓ centred in button
    int cx=bFBX+ADD_BTN_H/2;
    int cy=bFBY+ADD_BTN_W/2;
    SDL_Color sc = added ? COL_ADD_CLR : COL_GREEN;

    if(added)
    {
        // ✓ as a texture drawn with RenderCopyEx(90°) so it appears upright
        const int S=36;
        SDL_Surface* surf=SDL_CreateRGBSurface(0,S,S,32,
            0x00FF0000,0x0000FF00,0x000000FF,0xFF000000);
        if(surf){
            SDL_FillRect(surf,NULL,SDL_MapRGBA(surf->format,0,0,0,0));
            // Draw ✓ lines on surface (upright in surface space)
            SDL_LockSurface(surf);
            auto setpix=[&](int x,int y){
                if(x<0||y<0||x>=S||y>=S) return;
                Uint32* p=(Uint32*)((Uint8*)surf->pixels+y*surf->pitch+x*4);
                *p=SDL_MapRGBA(surf->format,sc.r,sc.g,sc.b,sc.a);
            };
            auto line=[&](int x0,int y0,int x1,int y1){
                int dx=abs(x1-x0),dy=abs(y1-y0);
                int steps=std::max(dx,dy); if(steps==0) return;
                float xi=(float)(x1-x0)/steps,yi=(float)(y1-y0)/steps;
                for(int i=0;i<=steps;i++){
                    int px=(int)(x0+xi*i),py=(int)(y0+yi*i);
                    for(int t=-2;t<=2;t++){setpix(px+t,py);setpix(px,py+t);}
                }
            };
            line(4, S/2,     S/3, S-6);
            line(S/3, S-6,   S-4, 6);
            SDL_UnlockSurface(surf);
            SDL_Texture* tex=SDL_CreateTextureFromSurface(r,surf);
            SDL_FreeSurface(surf);
            if(tex){
                SDL_Rect dst={cx-S/2,cy-S/2,S,S};
                SDL_Point piv = { S/2, S/2 };  // rotate around center
                SDL_RenderCopyEx(r, tex, NULL, &dst, 90.0, &piv, SDL_FLIP_NONE);
                SDL_DestroyTexture(tex);
            }
        }
    }
    else
    {
        // + using thick lines
        SDL_SetRenderDrawColor(r,sc.r,sc.g,sc.b,sc.a);
        int h=14;
        for(int t=-2;t<=2;t++){
            SDL_RenderDrawLine(r,cx-h,cy+t,cx+h,cy+t);
            SDL_RenderDrawLine(r,cx+t,cy-h,cx+t,cy+h);
        }
    }
    return btn;
}

/* ============================================================
   RENDER
============================================================ */
void fileBrowserRender(SDL_Renderer* r, TTF_Font* font)
{
    if(g_screen==FB_NONE) return;

    // Black background over everything
    SDL_SetRenderDrawColor(r,8,16,8,255);
    SDL_Rect full={0,0,FBW,FBH};
    SDL_RenderFillRect(r,&full);

    /* ========================================================
       SCREEN 1: MENU
       Layout (top → bottom on screen = low → high FB X):
         [MENU_MARGIN_TOP = 200]
         [MENU_TITLE_H=80]    "// ADD TO PLAYLIST"  (title, not selectable)
         [MENU_GAP=12]
         [MENU_ROW_H=130]     "Add FILES"            (menuSel=0, selectable)
         [MENU_GAP]
         [MENU_ROW_H=130]     "Add URL"              (menuSel=1, placeholder)
         [MENU_GAP]
         [MENU_ROW_H=130]     "A: SELECT   B: CANCEL" (hint, not selectable)
         [rest = bottom margin]
    ======================================================== */
    if(g_screen==FB_MENU)
    {
        int x = FBW - MENU_MARGIN_TOP;
        // --- TITLE LAST (so it appears at top of screen) ---
        x += MENU_TITLE_H + MENU_GAP;
        fbDrawRow(r, x, MENU_TITLE_H, COL_TITLE, COL_BORDER, 2);
        fbRowTextLeft(r, font, "// ADD TO PLAYLIST", x, MENU_TITLE_H, COL_GREEN, 30, -15);

        // --- rows (REVERSED ORDER!) ---
        struct { int id; const char* lbl; bool selectable; } rows[]={
            {1,  "Add URL",              true },
            {0,  "Add FILES",            true },
            {-1, "A: SELECT  B: CANCEL", false},
        };

        for(auto& row : rows)
        {
            x -= MENU_ROW_H;

            bool sel=(g_menuSel==row.id && row.selectable);
            SDL_Color bg  = sel ? COL_SEL   : COL_BLOCK;
            SDL_Color brd = sel ? COL_BORDER : COL_BRD_DIM;

            fbDrawRow(r, x, MENU_ROW_H, bg, brd, sel?3:1);

            SDL_Color tc = !row.selectable ? COL_GREY_DIM
                         : sel             ? COL_GREEN
                         :                   COL_GREY;

            fbRowText(r, font, row.lbl, x, MENU_ROW_H, tc, 20);

            x -= MENU_GAP;
        }



        return;
    }
    /* ========================================================
       SCREEN 2: BROWSE
       Layout (top → bottom on screen = low → high FB X):
         [BR_MARGIN_TOP = 200]
         [BR_HDR_H = 80]     "// SELECT FILES  [<] [>]"
         [BR_ROW_H×6 = 840]  file/folder rows
         [BR_BTNS_H = 80]    "[  Cancel  ]  [  Done  ]"
         [BR_HINT_H = 60]    hint text
         [rest = bottom margin ~360px]
    ======================================================== */
    if(g_screen==FB_BROWSE)
    {
        int total=(int)g_items.size();
        int visible=std::min(BR_ROWS, total-g_scroll);
        bool canBack = (g_scroll > 0);
        bool canFwd  = (g_scroll + BR_ROWS < total);

        int x = FBW - BR_MARGIN_TOP;

        /* ---- Header ---- */
        x -= BR_HDR_H;

        fbDrawRow(r, x, BR_HDR_H, COL_TITLE, COL_BORDER, 2);
        fbRowTextLeft(r, font, "// SELECT FILES", x, BR_HDR_H, COL_GREEN, 30, - 15);

        // [<] button — left of screen = low FB Y, right of "[<]" text
        if(canBack)
        {
            // Button sits at left of screen (low FB Y): y=40, w=80
            SDL_Rect lb={x+(BR_HDR_H-50)/2, FBH-40-180, 50, 80};
            fbDrawBox(r, lb, COL_BTN, COL_BORDER, 2);
            // Text centred in it
            //SDL_Rect tr={lb.x, lb.y, lb.w, lb.h};
            SDL_Rect tr = {lb.x + 30, lb.y, lb.w, lb.h};
            drawVerticalText(r,font,"<",tr,COL_GREEN,0,0,ALIGN_CENTER);
        }

        // [>] button — right of screen = high FB Y
        if(canFwd)
        {
            SDL_Rect rb={x+(BR_HDR_H-50)/2, FBH-40-80, 50, 80};
            fbDrawBox(r, rb, COL_BTN, COL_BORDER, 2);
            //SDL_Rect tr={rb.x, rb.y, rb.w, rb.h};
            SDL_Rect tr = {rb.x + 30, rb.y, rb.w, rb.h};
            drawVerticalText(r,font,">",tr,COL_GREEN,0,0,ALIGN_CENTER);
        }
        x -= BR_GAP;
        x += BR_HDR_H;

        /* ---- File / folder rows ---- */
        for(int vi = visible - 1; vi >= 0; vi--)
        {
            int idx = g_scroll + vi;
            FBItem& it = g_items[idx];
            bool isSel = (idx==g_sel);

            x -= BR_ROW_H;

            SDL_Color bg  = isSel ? COL_SEL   : COL_BLOCK;
            SDL_Color brd = isSel ? COL_BORDER : COL_BRD_DIM;

            fbDrawRow(r, x, BR_ROW_H, bg, brd, isSel?3:1);

            if(it.isDir) fbFolderIcon(r, x, BR_ROW_H);
            else         fbFormatBar(r, x, BR_ROW_H, it.name);

            char dn[300];
            if(!it.isDir&&isAudio(it.name))
                snprintf(dn,sizeof(dn),"%s %s",it.name,fmtTag(it.name));
            else
                snprintf(dn,sizeof(dn),"%s",it.name);

            SDL_Color tc = isSel    ? COL_GREEN
                         : it.isDir ? COL_FOLDER_CLR
                         :            COL_GREY;

            SDL_Rect tr={x, 0, BR_ROW_H, FBH};
            drawVerticalText(r, font, dn, tr, tc, - 15, 90, ALIGN_TOP);

            fbAddBtn(r, x, BR_ROW_H, it.added, isSel);
        }

        /* ---- Cancel / Done row ---- */
        {
          x -= BR_BTNS_H;

          int half = FBH/2 - 10;

          // Cancel LEFT
          SDL_Rect cancel={x, 5, BR_BTNS_H, half};

          fbDrawBox(r, cancel, COL_BLOCK, COL_BRD_DIM, 1);
//          drawVerticalText(r,font,"Cancel",cancel,COL_GREY,0,50,ALIGN_CENTER);
          SDL_Rect cancelText = {cancel.x + 10, cancel.y, cancel.w, cancel.h};
          drawVerticalText(r,font,"Cancel",cancelText,COL_GREY,0,0,ALIGN_CENTER);

          // Done RIGHT
          SDL_Rect done  ={x, FBH/2+5, BR_BTNS_H, half};
          fbDrawBox(r, done, COL_BTN_DONE, COL_BORDER, 2);
//          drawVerticalText(r,font,"Done",done,COL_GREEN,0,50,ALIGN_CENTER);
          SDL_Rect doneText = {done.x + 10, done.y, done.w, done.h};
          drawVerticalText(r,font,"Done",doneText,COL_GREEN,0,0,ALIGN_CENTER);



        }

        /* ---- Hint row ---- */
        {
          x -= BR_HINT_H;
          fbDrawRow(r, x, BR_HINT_H,
                    SDL_Color{4,14,4,200}, COL_BRD_DIM, 1);
          fbRowText(r, font,"A:Enter  B:Folder  X:Menu  Y:Done  +:Cancel",
          x, BR_HINT_H, COL_GREY_DIM, - 20);
        }

        return;
    }
}
