#include "settings.h"
#include "settings_state.h"
#include "ui.h"
#include "eq.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <algorithm>

/* ============================================================
   COORDINATE SYSTEM — same as fixed filebrowser.cpp
   FB rect {x, y, w, h}:
     x     = FB left  = screen top    (portrait)
     x+w   = FB right = screen bottom
     y     = FB top   = screen left
     y+h   = FB bot   = screen right
   Screen height (top→bottom) = FBW = 1920
   Screen width  (left→right) = FBH = 1080
   Rows drawn from HIGH FB X down to LOW FB X
   (high x = screen bottom, low x = screen top)
============================================================ */
#define FBW  1920
#define FBH  1080

// Layout — all FB X values
#define S_MARGIN_BOT   200   // blank at bottom of screen  = low FB X margin
#define S_MARGIN_TOP   400   // blank at top of screen     = high FB X margin
#define S_TITLE_H       80   // "// SETTINGS" title row
#define S_ROW_H        160   // each setting row height
#define S_GAP            8   // gap between rows
#define S_HINT_H        70   // hint row at bottom of screen
#define S_SAVE_H       120   // "Save Settings" + "Back" rows
#define S_VALUE_H       60   // value sub-row inside a setting row (at screen-right)
#define S_SLIDER_H      50   // slider track height
#define S_SLIDER_W     400   // slider track width (screen X span = FB Y span)
#define S_BTNS_H         80   // cancel/done row
#define ADD_BTN_W         70   // button width in FB-Y = screen horizontal size
#define ADD_BTN_H         70   // button height in FB-X = screen vertical size
#define ADD_BTN_MARGIN    10   // gap from right edge

/* ============================================================
   COLOURS (SDL_Color inline — same style as filebrowser)
============================================================ */
#define SC_BG          SDL_Color{  8,  16,  8, 255}
#define SC_BLOCK       SDL_Color{ 12,  24, 12, 220}
#define SC_SEL         SDL_Color{  0,  90,  0, 255}
#define SC_TITLE       SDL_Color{  4,  20,  4, 240}
#define SC_BORDER      SDL_Color{  0, 180,  0, 255}
#define SC_BRD_DIM     SDL_Color{  0,  60,  0, 180}
#define SC_GREEN       SDL_Color{  0, 255,  0, 255}
#define SC_GREEN_DIM   SDL_Color{  0, 180,  0, 200}
#define SC_WHITE       SDL_Color{255, 255,255, 255}
#define SC_GREY        SDL_Color{160, 160,160, 255}
#define SC_GREY_DIM    SDL_Color{100, 100,100, 200}
#define SC_SLIDER_BG   SDL_Color{ 60,  60,  60, 255}
#define SC_SLIDER_FILL SDL_Color{  0, 200,  80, 255}
#define COL_BTN        SDL_Color{ 15,  15, 15, 200}
#define COL_BTN_DONE   SDL_Color{  0,  80, 35, 200}

/* ============================================================
   STATE
============================================================ */
static bool g_settingsOpen  = false;
static int  g_selectedItem  = 0;

PlayerSettings g_settings =
{
    false,           // crossfadeEnabled
    3.0f,            // crossfadeSeconds
    false,           // autoGainEnabled
    REPLAYGAIN_TRACK // replayGainMode
};

void settingsOpen()  { g_settingsOpen = true; }
void settingsClose() { g_settingsOpen = false; }
bool settingsIsOpen(){ return g_settingsOpen; }

/* ============================================================
   INPUT
============================================================ */
void settingsHandleInput(PadState* pad)
{
    u64 down = padGetButtonsDown(pad);

    // X = close
    if(down & HidNpadButton_X){ settingsClose(); return; }

    // Up/Down navigate — on portrait screen Up = towards top = lower FB X,
    // which in our reversed layout means increasing index
    if(down & HidNpadButton_Down){
        g_selectedItem++;
        if(g_selectedItem >= SETTINGS_COUNT) g_selectedItem = 0;
    }
    if(down & HidNpadButton_Up){
        g_selectedItem--;
        if(g_selectedItem < 0) g_selectedItem = SETTINGS_COUNT - 1;
    }

    // A = toggle / activate
    if(down & HidNpadButton_A){
        switch(g_selectedItem){
            case SETTING_CROSSFADE:
                g_settings.crossfadeEnabled = !g_settings.crossfadeEnabled;
                break;
            case SETTING_CROSSFADE_TIME:
                g_settings.crossfadeSeconds = 3.0f; // reset to default
                break;
            case SETTING_REPLAYGAIN:
                // Cycle OFF → TRACK → ALBUM → OFF
                if(g_settings.replayGainMode == REPLAYGAIN_OFF)
                    g_settings.replayGainMode = REPLAYGAIN_TRACK;
                else if(g_settings.replayGainMode == REPLAYGAIN_TRACK)
                    g_settings.replayGainMode = REPLAYGAIN_ALBUM;
                else
                    g_settings.replayGainMode = REPLAYGAIN_OFF;
                break;
            case SETTING_AUTOGAIN:
                g_settings.autoGainEnabled = !g_settings.autoGainEnabled;
                break;
            case SETTING_SAVESETTINGS:
                settingsClose();
                break;
            case SETTING_BACK:
                settingsClose();
                break;
        }
    }

    // Left/Right = adjust value for selected item
    if((down & HidNpadButton_Left) || (down & HidNpadButton_Right))
    {
        float step = (down & HidNpadButton_Left) ? -0.5f : 0.5f;

        if(g_selectedItem == SETTING_CROSSFADE_TIME)
        {
            g_settings.crossfadeSeconds -= step;
            if(g_settings.crossfadeSeconds < 0.5f)  g_settings.crossfadeSeconds = 0.5f;
            if(g_settings.crossfadeSeconds > 10.0f) g_settings.crossfadeSeconds = 10.0f;
        }
        else if(g_selectedItem == SETTING_REPLAYGAIN)
        {
            // Left/Right also cycles ReplayGain mode
            if(down & HidNpadButton_Right){
                if(g_settings.replayGainMode == REPLAYGAIN_OFF)
                    g_settings.replayGainMode = REPLAYGAIN_TRACK;
                else if(g_settings.replayGainMode == REPLAYGAIN_TRACK)
                    g_settings.replayGainMode = REPLAYGAIN_ALBUM;
                else
                    g_settings.replayGainMode = REPLAYGAIN_OFF;
            } else {
                if(g_settings.replayGainMode == REPLAYGAIN_OFF)
                    g_settings.replayGainMode = REPLAYGAIN_ALBUM;
                else if(g_settings.replayGainMode == REPLAYGAIN_ALBUM)
                    g_settings.replayGainMode = REPLAYGAIN_TRACK;
                else
                    g_settings.replayGainMode = REPLAYGAIN_OFF;
            }
        }
    }
}

/* ============================================================
   DRAW HELPERS  (same pattern as filebrowser)
============================================================ */
static void sDrawBox(SDL_Renderer* r, SDL_Rect rect,
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

// Draw a full-width row at FB X = fbx, height fbw
static void sDrawRow(SDL_Renderer* r, int fbx, int fbw,
                     SDL_Color fill, SDL_Color border, int thick=2)
{
    SDL_Rect rect={fbx, 0, fbw, FBH};
    sDrawBox(r, rect, fill, border, thick);
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

// Draw text centred horizontally in a row (ALIGN_CENTER on full FBH)
static void sRowText(SDL_Renderer* r, TTF_Font* font,
                     const char* txt, int fbx, int fbw, SDL_Color col, int offsetY = 0)
{
    SDL_Rect rect={fbx - offsetY, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, 0, 0, ALIGN_CENTER);
}

// Draw label text near the top of the screen (low FB Y = screen left)
// paddingY = screen X offset from screen left
static void sRowLabel(SDL_Renderer* r, TTF_Font* font,
                      const char* txt, int fbx, int fbw,
                      SDL_Color col, int paddingY=30)
{
    SDL_Rect rect={fbx, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, -15, paddingY, ALIGN_TOP);
}

// Draw value text near the bottom of the screen (high FB Y = screen right)
// paddingY = screen X offset from screen right
static void sRowValue(SDL_Renderer* r, TTF_Font* font,
                      const char* txt, int fbx, int fbw,
                      SDL_Color col, int paddingY=30)
{
    SDL_Rect rect={fbx, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, -15, paddingY, ALIGN_BOTTOM);
}

// Draw a vertical slider bar (appears as a vertical bar on the portrait screen)
// fbx = FB X of row, fbw = row height
// value = 0.0..1.0
static void sDrawSlider(SDL_Renderer* r, int fbx, int fbw, float value)
{
    // Slider sits at screen right = high FB Y, centred in the row
    // In FB coords: centred in FBH, near right side (high FB Y)
    int sliderFBY_centre = FBH - 520;  // screen X ~120px from right = FB Y
    int trackFBY = sliderFBY_centre - S_SLIDER_W/2;
    int trackFBX = fbx + (fbw - S_SLIDER_H)/2;  // centred vertically in row

    // Background track
    SDL_Rect bg={trackFBX, trackFBY, S_SLIDER_H, S_SLIDER_W};
    sDrawBox(r, bg, SC_SLIDER_BG, SC_BRD_DIM, 1);

    // Fill
    int fillH = (int)(S_SLIDER_W * value);
    if(fillH > 0){
        SDL_Rect fill={trackFBX, trackFBY + S_SLIDER_W - fillH,
                       S_SLIDER_H, fillH};
        drawRect(r, fill,
                 SC_SLIDER_FILL.b, SC_SLIDER_FILL.a,
                 SC_SLIDER_FILL.r, SC_SLIDER_FILL.g);
    }

    // Knob — a small square at the fill level
    int knobY = trackFBY + S_SLIDER_W - fillH - 8;
    if(knobY < trackFBY) knobY = trackFBY;
    SDL_Rect knob={trackFBX - 4, knobY, S_SLIDER_H + 8, 16};
    sDrawBox(r, knob, SDL_Color{30,30,30,255}, SC_GREEN_DIM, 1);
}

// Draw an ON/OFF toggle indicator box at screen-right of a row
/* ============================================================
   RENDER
   Rows drawn from high FB X → low FB X (= screen bottom → top)
   Order in array matches screen top → bottom, but we draw reversed.
============================================================ */
void settingsRender(SDL_Renderer* renderer, TTF_Font* font)
{
    if(!g_settingsOpen) return;

    // Dark overlay
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 5, 12, 5, 240);
    SDL_Rect overlay={0,0,FBW,FBH};
    SDL_RenderFillRect(renderer, &overlay);

    /* Layout (screen top → bottom = decreasing FB X):
       Screen top    (high FB X end):
         [S_MARGIN_TOP]         blank
         [S_TITLE_H]            "// SETTINGS"
         [S_ROW_H]              Crossfade        (SETTING_CROSSFADE)
         [S_GAP]
         [S_ROW_H]              Crossfade Time   (SETTING_CROSSFADE_TIME)
         [S_GAP]
         [S_ROW_H]              ReplayGain       (SETTING_REPLAYGAIN)
         [S_GAP]
         [S_ROW_H]              Auto Gain        (SETTING_AUTOGAIN)
         [S_GAP]
         [S_SAVE_H]             Save Settings    (SETTING_AUTO_EQ — repurposed as Save)
         [S_GAP]
         [S_SAVE_H]             Back             (SETTING_BACK)
         [S_HINT_H]             hint
       Screen bottom (low FB X end):
         [S_MARGIN_BOT]         blank
    */

    // Start position: high FB X end, walk down
    int x = FBW - S_MARGIN_TOP;

    // Title
    x -= S_TITLE_H;
    sDrawRow(renderer, x, S_TITLE_H, SC_TITLE, SC_BORDER, 2);
    fbRowTextLeft(renderer, font, "// SETTINGS", x, S_TITLE_H, SC_GREEN, 30, -15);

    // Setting rows — defined top→bottom (high FB X → low FB X)
    struct SettingRow {
        int     id;
        const char* label;
        bool    isBack;
        bool    isSave;
    };
    SettingRow srows[] = {
        { SETTING_CROSSFADE,        "Crossfade",      false, false },
        { SETTING_CROSSFADE_TIME,   "Crossfade Time", false, false },
        { SETTING_REPLAYGAIN,       "ReplayGain",     false, false },
        { SETTING_AUTOGAIN,         "Auto Gain",      false, false },
//        { SETTING_SAVESETTINGS,     "Save Settings",  false, true  },
//        { SETTING_BACK,             "Back",           true,  false },
    };

    for(auto& sr : srows)
    {
        int rowH = (sr.isBack || sr.isSave) ? S_SAVE_H : S_ROW_H;
        x -= (rowH + S_GAP);

        bool sel = (g_selectedItem == sr.id);
        SDL_Color bg  = sel ? SC_SEL   : SC_BLOCK;
        SDL_Color brd = sel ? SC_BORDER : SC_BRD_DIM;
        SDL_Color tc  = sel ? SC_GREEN  : SC_WHITE;

        sDrawRow(renderer, x, rowH, bg, brd, sel?3:1);

        // Label — top of screen side (screen left = low FB Y = ALIGN_TOP)
        sRowLabel(renderer, font, sr.label, x, rowH, tc, 30);

        if(sr.isSave || sr.isBack)
        {
            // No value for Back/Save — label centred
            // sRowText(renderer, font, sr.label, x, rowH, tc);
        }
        else
        {
            // Value at bottom of screen side (screen right = ALIGN_BOTTOM)
            char val[32]={};
            switch(sr.id){
                case SETTING_CROSSFADE:
                    snprintf(val,sizeof(val),"%s",
                             g_settings.crossfadeEnabled ? "ON" : "OFF");
                    //sRowValue(renderer, font, val, x, rowH,g_settings.crossfadeEnabled ? SC_GREEN : SC_GREY, 30);
                    // Toggle box
                    {
                        const int BW=50, BH=36;
                        int by = FBH - BW - 20;
                        int bx = x + (rowH - BH)/2;
                        SDL_Color bbg = g_settings.crossfadeEnabled
                                      ? SDL_Color{0,120,0,255}
                                      : SDL_Color{35,35,35,255};
                        SDL_Color bbr = g_settings.crossfadeEnabled
                                      ? SC_GREEN : SC_BRD_DIM;
                        SDL_Rect box={bx,by,BH,BW};
                        sDrawBox(renderer, box, bbg, bbr, 2);
                        SDL_Rect tr={bx,by,BH,BW};
                        //drawVerticalText(renderer,font,g_settings.crossfadeEnabled?"ON":"OFF",tr,g_settings.crossfadeEnabled?SC_GREEN:SC_GREY,0,0,ALIGN_CENTER);
                    }
                    break;

                case SETTING_CROSSFADE_TIME:
                    {
                        snprintf(val,sizeof(val),"%.1fs",
                                 g_settings.crossfadeSeconds);
                        sRowValue(renderer, font, val, x, rowH, SC_GREEN_DIM, 30);

                        // Slider
                        float t=(g_settings.crossfadeSeconds-0.5f)/(10.0f-0.5f);
                        sDrawSlider(renderer, x, rowH, t);
                    }
                    break;

                case SETTING_REPLAYGAIN:
                    {
                        const char* m="OFF";
                        if(g_settings.replayGainMode==REPLAYGAIN_TRACK) m="TRACK";
                        if(g_settings.replayGainMode==REPLAYGAIN_ALBUM) m="ALBUM";
                        sRowValue(renderer, font, m, x, rowH, SC_GREEN_DIM, 30);
                    }
                    break;

                case SETTING_AUTOGAIN:
                    {
                        snprintf(val,sizeof(val),"%s",
                                 g_settings.autoGainEnabled ? "ON" : "OFF");
                        //sRowValue(renderer, font, val, x, rowH,g_settings.autoGainEnabled ? SC_GREEN : SC_GREY, 30);
                        // Toggle box
                        {
                            const int BW=50, BH=36;
                            int by = FBH - BW - 20;
                            int bx = x + (rowH - BH)/2;
                            SDL_Color bbg = g_settings.autoGainEnabled
                                          ? SDL_Color{0,120,0,255}
                                          : SDL_Color{35,35,35,255};
                            SDL_Color bbr = g_settings.autoGainEnabled
                                          ? SC_GREEN : SC_BRD_DIM;
                            SDL_Rect box={bx,by,BH,BW};
                            sDrawBox(renderer, box, bbg, bbr, 2);
                            SDL_Rect tr={bx,by,BH,BW};
                            //drawVerticalText(renderer,font,g_settings.autoGainEnabled?"ON":"OFF",tr,g_settings.autoGainEnabled?SC_GREEN:SC_GREY,0,0,ALIGN_CENTER);
                        }
                    }
                    break;
            }
        }
    }
    x -= S_GAP;
    x -= S_BTNS_H;

    int half = FBH/2 - 10;

    // Save Settings LEFT
    SDL_Rect cancel={x, 5, S_BTNS_H, half};

    sDrawBox(renderer, cancel, SC_BLOCK, SC_BRD_DIM, 1);
//          drawVerticalText(r,font,"Cancel",cancel,COL_GREY,0,50,ALIGN_CENTER);
    SDL_Rect cancelText = {cancel.x + 10, cancel.y, cancel.w, cancel.h};
    drawVerticalText(renderer,font,"Save Settings",cancelText,SC_WHITE,0,0,ALIGN_CENTER);

    // Done RIGHT
    SDL_Rect done  ={x, FBH/2+5, S_BTNS_H, half};
    sDrawBox(renderer, done, SC_BLOCK, SC_BRD_DIM, 2);
//          drawVerticalText(r,font,"Done",done,COL_GREEN,0,50,ALIGN_CENTER);
    SDL_Rect doneText = {done.x + 10, done.y, done.w, done.h};
    drawVerticalText(renderer,font,"Back",doneText,SC_WHITE,0,0,ALIGN_CENTER);


    // Hint row at very bottom (lowest FB X area)
    x -= (S_HINT_H + S_GAP);
    sDrawRow(renderer, x, S_HINT_H,
             SDL_Color{4,14,4,200}, SC_BRD_DIM, 1);
    {
        //SDL_Rect hr={x, 0, S_HINT_H, FBH};
        sRowText(renderer, font,
            "Move: Up or Down    Select: A or Left or Right",
            x, S_HINT_H, SC_GREY_DIM, -15);
    }
}
