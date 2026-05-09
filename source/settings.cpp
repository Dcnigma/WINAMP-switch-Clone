#include "settings.h"
#include "settings_state.h"
#include "ui.h"
#include "eq.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>

/* ============================================================
   COORDINATE SYSTEM
============================================================ */
#define FBW  1920
#define FBH  1080
#define S_MARGIN_BOT   200
#define S_MARGIN_TOP   400
#define S_HDR_H         80
#define S_TITLE_H       80
#define S_ROW_H        160
#define S_GAP            8
#define S_HINT_H        70
#define S_SAVE_H       120
#define S_VALUE_H       60
#define S_SLIDER_H      50
#define S_SLIDER_W     400
#define S_BTNS_H        80
#define ADD_BTN_W       70
#define ADD_BTN_H       70
#define ADD_BTN_MARGIN  10

/* ============================================================
   COLOURS
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
   PAGE NAVIGATION STATE
============================================================ */
static int  g_settingsPage   = 0;
static const int SETTINGS_PAGES = 2;
#define SETTING_PAGE_NEXT  (SETTINGS_COUNT)
#define SETTING_PAGE_PREV  (SETTINGS_COUNT + 1)
static int  g_totalItems = SETTINGS_COUNT;

/* ============================================================
   STATE
============================================================ */
static bool g_settingsOpen = false;
static int  g_selectedItem = 0;

PlayerSettings g_settings =
{
    false,            // crossfadeEnabled
    3.0f,             // crossfadeSeconds
    false,            // autoGainEnabled
    REPLAYGAIN_TRACK, // replayGainMode
    45.0f,            // touchSensitivity
    3,                // touchSpeedLimit
    true              // stayAwakeEnabled
};

void settingsOpen()  { g_settingsOpen = true; }
void settingsClose() { g_settingsOpen = false; }
bool settingsIsOpen(){ return g_settingsOpen; }

/* ============================================================
   PAGE NAVIGATION HELPERS
============================================================ */
int settingsGetCurrentPage() { return g_settingsPage; }

void settingsPrevPage()
{
    if (g_settingsPage > 0) { g_settingsPage--; g_selectedItem = 0; }
}

void settingsNextPage()
{
    if (g_settingsPage < SETTINGS_PAGES - 1) { g_settingsPage++; g_selectedItem = 0; }
}

/* ============================================================
   SETTINGS SAVE
============================================================ */
void settingsSave()
{
    mkdir("sdmc:/config",        0777);
    mkdir("sdmc:/config/winamp", 0777);

    FILE* f = fopen("sdmc:/config/winamp/settings.json", "w");
    if (!f) { printf("[Settings] Failed to open settings.json for writing\n"); return; }

    const char* replayGainStr =
        (g_settings.replayGainMode == REPLAYGAIN_TRACK) ? "TRACK" :
        (g_settings.replayGainMode == REPLAYGAIN_ALBUM) ? "ALBUM" : "OFF";

    fprintf(f,
        "{\n"
        "  \"crossfadeEnabled\": %s,\n"
        "  \"crossfadeSeconds\": %.1f,\n"
        "  \"autoGainEnabled\": %s,\n"
        "  \"replayGainMode\": \"%s\",\n"
        "  \"touchSensitivity\": %.1f,\n"
        "  \"touchSpeedLimit\": %d,\n"
        "  \"stayAwakeEnabled\": %s\n"
        "}\n",
        g_settings.crossfadeEnabled ? "true" : "false",
        g_settings.crossfadeSeconds,
        g_settings.autoGainEnabled  ? "true" : "false",
        replayGainStr,
        g_settings.touchSensitivity,
        g_settings.touchSpeedLimit,
        g_settings.stayAwakeEnabled ? "true" : "false"
    );

    fclose(f);
    printf("[Settings] Saved to sdmc:/config/winamp/settings.json\n");
}

/* ============================================================
   SETTINGS LOAD
============================================================ */
void settingsLoad()
{
    FILE* f = fopen("sdmc:/config/winamp/settings.json", "r");
    if (!f)
    {
        printf("[Settings] No settings.json found, using defaults\n");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 4096)
    {
        printf("[Settings] settings.json invalid size, using defaults\n");
        fclose(f);
        return;
    }

    char* buf = (char*)malloc(size + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);

    // Simple key/value parser — no external JSON library needed
    auto readBool = [&](const char* key, bool& out)
    {
        const char* p = strstr(buf, key);
        if (!p) return;
        p = strchr(p, ':');
        if (!p) return;
        while (*++p == ' ');
        out = (strncmp(p, "true", 4) == 0);
    };

    auto readFloat = [&](const char* key, float& out, float minV, float maxV)
    {
        const char* p = strstr(buf, key);
        if (!p) return;
        p = strchr(p, ':');
        if (!p) return;
        float v = 0.0f;
        if (sscanf(p + 1, " %f", &v) == 1)
            if (v >= minV && v <= maxV) out = v;
    };

    auto readInt = [&](const char* key, int& out, int minV, int maxV)
    {
        const char* p = strstr(buf, key);
        if (!p) return;
        p = strchr(p, ':');
        if (!p) return;
        int v = 0;
        if (sscanf(p + 1, " %d", &v) == 1)
            if (v >= minV && v <= maxV) out = v;
    };

    auto readString = [&](const char* key, char* out, int maxLen)
    {
        const char* p = strstr(buf, key);
        if (!p) return;
        p = strchr(p, ':');
        if (!p) return;
        p = strchr(p, '"');
        if (!p) return;
        p++;
        int i = 0;
        while (*p && *p != '"' && i < maxLen - 1)
            out[i++] = *p++;
        out[i] = '\0';
    };

    // Page 1
    readBool ("\"crossfadeEnabled\"",  g_settings.crossfadeEnabled);
    readFloat("\"crossfadeSeconds\"",  g_settings.crossfadeSeconds,  0.5f, 10.0f);
    readBool ("\"autoGainEnabled\"",   g_settings.autoGainEnabled);

    char replayGainStr[16] = {};
    readString("\"replayGainMode\"", replayGainStr, sizeof(replayGainStr));
    if      (strcmp(replayGainStr, "TRACK") == 0) g_settings.replayGainMode = REPLAYGAIN_TRACK;
    else if (strcmp(replayGainStr, "ALBUM") == 0) g_settings.replayGainMode = REPLAYGAIN_ALBUM;
    else if (strcmp(replayGainStr, "OFF")   == 0) g_settings.replayGainMode = REPLAYGAIN_OFF;

    // Page 2
    readFloat("\"touchSensitivity\"",  g_settings.touchSensitivity,  20.0f, 80.0f);
    readInt  ("\"touchSpeedLimit\"",   g_settings.touchSpeedLimit,   1,     10);
    readBool ("\"stayAwakeEnabled\"",  g_settings.stayAwakeEnabled);

    free(buf);

    printf("[Settings] Loaded: crossfade=%s %.1fs  gain=%s  replayGain=%s\n",
           g_settings.crossfadeEnabled ? "on" : "off",
           g_settings.crossfadeSeconds,
           g_settings.autoGainEnabled  ? "on" : "off",
           replayGainStr[0] ? replayGainStr : "OFF");
    printf("[Settings]   sensitivity=%.0fpx  speedLimit=%d  stayAwake=%s\n",
           g_settings.touchSensitivity, g_settings.touchSpeedLimit,
           g_settings.stayAwakeEnabled ? "on" : "off");
}

/* ============================================================
   INPUT
============================================================ */
void settingsHandleInput(PadState* pad)
{
    u64 down = padGetButtonsDown(pad);

    if (down & HidNpadButton_X) { settingsClose(); return; }

    int pageItemCount = (g_settingsPage == 0) ? 4 : 3;
    int totalSelectableItems = pageItemCount + 2;  // +2 for Save and Back

    if (down & HidNpadButton_Up)
    {
        g_selectedItem--;
        if (g_selectedItem < 0) g_selectedItem = totalSelectableItems - 1;
    }
    if (down & HidNpadButton_Down)
    {
        g_selectedItem++;
        if (g_selectedItem >= totalSelectableItems) g_selectedItem = 0;
    }

    if (down & HidNpadButton_A)
    {
        if (g_selectedItem == pageItemCount)
            { settingsSave(); settingsClose(); return; }
        if (g_selectedItem == pageItemCount + 1)
            { settingsClose(); return; }

        int act = g_selectedItem;
        if (g_settingsPage == 1) act = SETTING_TOUCH_SENSITIVITY + g_selectedItem;

        switch (act)
        {
            case SETTING_CROSSFADE:
                g_settings.crossfadeEnabled = !g_settings.crossfadeEnabled; break;
            case SETTING_CROSSFADE_TIME:
                g_settings.crossfadeSeconds = 3.0f; break;
            case SETTING_REPLAYGAIN:
                if      (g_settings.replayGainMode == REPLAYGAIN_OFF)   g_settings.replayGainMode = REPLAYGAIN_TRACK;
                else if (g_settings.replayGainMode == REPLAYGAIN_TRACK) g_settings.replayGainMode = REPLAYGAIN_ALBUM;
                else                                                      g_settings.replayGainMode = REPLAYGAIN_OFF;
                break;
            case SETTING_AUTOGAIN:
                g_settings.autoGainEnabled = !g_settings.autoGainEnabled; break;
            case SETTING_TOUCH_SENSITIVITY:
                g_settings.touchSensitivity = 45.0f; break;
            case SETTING_TOUCH_SPEED_LIMIT:
                g_settings.touchSpeedLimit = 3; break;
            case SETTING_STAY_AWAKE:
                g_settings.stayAwakeEnabled = !g_settings.stayAwakeEnabled; break;
        }
    }

    if ((down & HidNpadButton_Left) || (down & HidNpadButton_Right))
    {
        if (g_selectedItem >= pageItemCount) return;

        int act = g_selectedItem;
        if (g_settingsPage == 1) act = SETTING_TOUCH_SENSITIVITY + g_selectedItem;

        if (act == SETTING_CROSSFADE_TIME)
        {
            float step = (down & HidNpadButton_Left) ? -0.5f : 0.5f;
            g_settings.crossfadeSeconds += step;
            if (g_settings.crossfadeSeconds < 0.5f)  g_settings.crossfadeSeconds = 0.5f;
            if (g_settings.crossfadeSeconds > 10.0f) g_settings.crossfadeSeconds = 10.0f;
        }
        else if (act == SETTING_TOUCH_SENSITIVITY)
        {
            float step = (down & HidNpadButton_Left) ? -5.0f : 5.0f;
            g_settings.touchSensitivity += step;
            if (g_settings.touchSensitivity < 20.0f) g_settings.touchSensitivity = 20.0f;
            if (g_settings.touchSensitivity > 80.0f) g_settings.touchSensitivity = 80.0f;
        }
        else if (act == SETTING_TOUCH_SPEED_LIMIT)
        {
            int step = (down & HidNpadButton_Left) ? -1 : 1;
            g_settings.touchSpeedLimit += step;
            if (g_settings.touchSpeedLimit < 1)  g_settings.touchSpeedLimit = 1;
            if (g_settings.touchSpeedLimit > 10) g_settings.touchSpeedLimit = 10;
        }
        else if (act == SETTING_REPLAYGAIN)
        {
            if (down & HidNpadButton_Right)
            {
                if      (g_settings.replayGainMode == REPLAYGAIN_OFF)   g_settings.replayGainMode = REPLAYGAIN_TRACK;
                else if (g_settings.replayGainMode == REPLAYGAIN_TRACK) g_settings.replayGainMode = REPLAYGAIN_ALBUM;
                else                                                      g_settings.replayGainMode = REPLAYGAIN_OFF;
            }
            else
            {
                if      (g_settings.replayGainMode == REPLAYGAIN_OFF)   g_settings.replayGainMode = REPLAYGAIN_ALBUM;
                else if (g_settings.replayGainMode == REPLAYGAIN_ALBUM) g_settings.replayGainMode = REPLAYGAIN_TRACK;
                else                                                      g_settings.replayGainMode = REPLAYGAIN_OFF;
            }
        }
    }

    if (down & HidNpadButton_ZL) settingsPrevPage();
    if (down & HidNpadButton_ZR) settingsNextPage();
}

/* ============================================================
   DRAW HELPERS
============================================================ */
static void sDrawBox(SDL_Renderer* r, SDL_Rect rect,
                     SDL_Color fill, SDL_Color border, int thick=2)
{
    drawRect(r, rect, fill.r, fill.g, fill.b, fill.a);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, border.r, border.g, border.b, border.a);
    for (int i = 0; i < thick; i++) {
        SDL_Rect br = {rect.x+i, rect.y+i, rect.w-i*2, rect.h-i*2};
        SDL_RenderDrawRect(r, &br);
    }
}

static void sDrawRow(SDL_Renderer* r, int fbx, int fbw,
                     SDL_Color fill, SDL_Color border, int thick=2)
{
    SDL_Rect rect = {fbx, 0, fbw, FBH};
    sDrawBox(r, rect, fill, border, thick);
}

static void fbRowTextLeft(SDL_Renderer* r, TTF_Font* font,
                          const char* txt, int fbx, int fbw,
                          SDL_Color col, int screenLeftPad=30, int offsetY=0)
{
    SDL_Rect rect = {fbx - offsetY, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, 0, screenLeftPad, ALIGN_TOP);
}

static void sRowText(SDL_Renderer* r, TTF_Font* font,
                     const char* txt, int fbx, int fbw, SDL_Color col, int offsetY=0)
{
    SDL_Rect rect = {fbx - offsetY, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, 0, 0, ALIGN_CENTER);
}

static void sRowLabel(SDL_Renderer* r, TTF_Font* font,
                      const char* txt, int fbx, int fbw,
                      SDL_Color col, int paddingY=30)
{
    SDL_Rect rect = {fbx, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, -15, paddingY, ALIGN_TOP);
}

static void sRowValue(SDL_Renderer* r, TTF_Font* font,
                      const char* txt, int fbx, int fbw,
                      SDL_Color col, int paddingY=30)
{
    SDL_Rect rect = {fbx, 0, fbw, FBH};
    drawVerticalText(r, font, txt, rect, col, -15, paddingY, ALIGN_BOTTOM);
}

static void sDrawSlider(SDL_Renderer* r, int fbx, int fbw, float value)
{
    int sliderFBY_centre = FBH - 520;
    int trackFBY = sliderFBY_centre - S_SLIDER_W/2;
    int trackFBX = fbx + (fbw - S_SLIDER_H)/2;

    SDL_Rect bg = {trackFBX, trackFBY, S_SLIDER_H, S_SLIDER_W};
    sDrawBox(r, bg, SC_SLIDER_BG, SC_BRD_DIM, 1);

    int fillH = (int)(S_SLIDER_W * value);
    if (fillH > 0) {
        SDL_Rect fill = {trackFBX, trackFBY + S_SLIDER_W - fillH, S_SLIDER_H, fillH};
        drawRect(r, fill, SC_SLIDER_FILL.b, SC_SLIDER_FILL.a,
                 SC_SLIDER_FILL.r, SC_SLIDER_FILL.g);
    }

    int knobY = trackFBY + S_SLIDER_W - fillH - 8;
    if (knobY < trackFBY) knobY = trackFBY;
    SDL_Rect knob = {trackFBX - 4, knobY, S_SLIDER_H + 8, 16};
    sDrawBox(r, knob, SDL_Color{30,30,30,255}, SC_GREEN_DIM, 1);
}

/* ============================================================
   RENDER
============================================================ */
void settingsRender(SDL_Renderer* renderer, TTF_Font* font)
{
    if (!g_settingsOpen) return;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 5, 12, 5, 240);
    SDL_Rect overlay = {0, 0, FBW, FBH};
    SDL_RenderFillRect(renderer, &overlay);

    int x = FBW - S_MARGIN_TOP;

    x -= S_TITLE_H;
    sDrawRow(renderer, x, S_TITLE_H, SC_TITLE, SC_BORDER, 2);
    char titleText[64];
    snprintf(titleText, sizeof(titleText), "// SETTINGS - Page %d/%d",
             g_settingsPage + 1, SETTINGS_PAGES);
    fbRowTextLeft(renderer, font, titleText, x, S_TITLE_H, SC_GREEN, 30, -15);

    bool canBack = (g_settingsPage > 0);
    bool canFwd  = (g_settingsPage < SETTINGS_PAGES - 1);

    if (canBack) {
        bool s = (g_selectedItem == SETTING_PAGE_PREV);
        SDL_Rect lb = {x + (S_HDR_H-50)/2, FBH-40-180, 50, 80};
        sDrawBox(renderer, lb, s ? SC_SEL : COL_BTN, s ? SC_BORDER : SC_BRD_DIM, s ? 3 : 2);
        SDL_Rect tr = {lb.x+30, lb.y, lb.w, lb.h};
        drawVerticalText(renderer, font, "<", tr, s ? SC_GREEN : SC_GREY, 0, 0, ALIGN_CENTER);
    }
    if (canFwd) {
        bool s = (g_selectedItem == SETTING_PAGE_NEXT);
        SDL_Rect rb = {x + (S_HDR_H-50)/2, FBH-40-80, 50, 80};
        sDrawBox(renderer, rb, s ? SC_SEL : COL_BTN, s ? SC_BORDER : SC_BRD_DIM, s ? 3 : 2);
        SDL_Rect tr = {rb.x+30, rb.y, rb.w, rb.h};
        drawVerticalText(renderer, font, ">", tr, s ? SC_GREEN : SC_GREY, 0, 0, ALIGN_CENTER);
    }

    struct SettingRow { int id; const char* label; bool isBack; bool isSave; };

    static SettingRow page1[] = {
        { SETTING_CROSSFADE,      "Crossfade",      false, false },
        { SETTING_CROSSFADE_TIME, "Crossfade Time", false, false },
        { SETTING_REPLAYGAIN,     "ReplayGain",     false, false },
        { SETTING_AUTOGAIN,       "Auto Gain",      false, false },
    };
    static SettingRow page2[] = {
        { SETTING_TOUCH_SENSITIVITY, "Touch Sensitivity", false, false },
        { SETTING_TOUCH_SPEED_LIMIT, "Touch Speed",       false, false },
        { SETTING_STAY_AWAKE,        "Stay Awake",        false, false },
    };

    SettingRow* srows  = (g_settingsPage == 0) ? page1 : page2;
    int         rowCount = (g_settingsPage == 0) ? 4 : 3;

    for (int i = 0; i < rowCount; i++)
    {
        SettingRow& sr = srows[i];
        int rowH = (sr.isBack || sr.isSave) ? S_SAVE_H : S_ROW_H;
        x -= (rowH + S_GAP);

        bool sel = (g_selectedItem == i);
        SDL_Color bg  = sel ? SC_SEL   : SC_BLOCK;
        SDL_Color brd = sel ? SC_BORDER : SC_BRD_DIM;
        SDL_Color tc  = sel ? SC_GREEN  : SC_WHITE;

        sDrawRow(renderer, x, rowH, bg, brd, sel ? 3 : 1);
        sRowLabel(renderer, font, sr.label, x, rowH, tc, 30);

        char val[32] = {};
        switch (sr.id)
        {
            case SETTING_CROSSFADE:
            case SETTING_AUTOGAIN:
            case SETTING_STAY_AWAKE:
            {
                bool on = (sr.id == SETTING_CROSSFADE)  ? g_settings.crossfadeEnabled :
                          (sr.id == SETTING_AUTOGAIN)   ? g_settings.autoGainEnabled  :
                                                           g_settings.stayAwakeEnabled;
                const int BW=100, BH=100;
                SDL_Rect box = {x + (rowH-BH)/2, FBH-BW-20, BH, BW};
                sDrawBox(renderer, box,
                         on ? SDL_Color{0,120,0,255} : SDL_Color{35,35,35,255},
                         on ? SC_GREEN : SC_BRD_DIM, 2);
                break;
            }
            case SETTING_CROSSFADE_TIME:
            {
                snprintf(val, sizeof(val), "%.1fs", g_settings.crossfadeSeconds);
                sRowValue(renderer, font, val, x, rowH, SC_GREEN_DIM, 30);
                float t = (g_settings.crossfadeSeconds - 0.5f) / (10.0f - 0.5f);
                sDrawSlider(renderer, x, rowH, t);
                break;
            }
            case SETTING_REPLAYGAIN:
            {
                const char* m = (g_settings.replayGainMode == REPLAYGAIN_TRACK) ? "TRACK" :
                                (g_settings.replayGainMode == REPLAYGAIN_ALBUM) ? "ALBUM" : "OFF";
                sRowValue(renderer, font, m, x, rowH, SC_GREEN_DIM, 30);
                break;
            }
            case SETTING_TOUCH_SENSITIVITY:
            {
                snprintf(val, sizeof(val), "%.0f px", g_settings.touchSensitivity);
                sRowValue(renderer, font, val, x, rowH, SC_GREEN_DIM, 30);
                float t = (g_settings.touchSensitivity - 20.0f) / (80.0f - 20.0f);
                sDrawSlider(renderer, x, rowH, t);
                break;
            }
            case SETTING_TOUCH_SPEED_LIMIT:
            {
                snprintf(val, sizeof(val), "%d", g_settings.touchSpeedLimit);
                sRowValue(renderer, font, val, x, rowH, SC_GREEN_DIM, 30);
                break;
            }
        }
    }

    x -= S_GAP;
    x -= S_BTNS_H;
    int half = FBH/2 - 10;

    {
        bool sel = (g_selectedItem == rowCount);
        SDL_Color bg = sel ? SC_SEL : SC_BLOCK, brd = sel ? SC_BORDER : SC_BRD_DIM, tc = sel ? SC_GREEN : SC_WHITE;
        SDL_Rect btn = {x, 5, S_BTNS_H, half};
        sDrawBox(renderer, btn, bg, brd, sel ? 3 : 1);
        SDL_Rect tr  = {btn.x+10, btn.y, btn.w, btn.h};
        drawVerticalText(renderer, font, "Save Settings", tr, tc, 0, 0, ALIGN_CENTER);
    }
    {
        bool sel = (g_selectedItem == rowCount + 1);
        SDL_Color bg = sel ? SC_SEL : SC_BLOCK, brd = sel ? SC_BORDER : SC_BRD_DIM, tc = sel ? SC_GREEN : SC_WHITE;
        SDL_Rect btn = {x, FBH/2+5, S_BTNS_H, half};
        sDrawBox(renderer, btn, bg, brd, sel ? 3 : 1);
        SDL_Rect tr  = {btn.x+10, btn.y, btn.w, btn.h};
        drawVerticalText(renderer, font, "Back", tr, tc, 0, 0, ALIGN_CENTER);
    }

    x -= (S_HINT_H + S_GAP);
    sDrawRow(renderer, x, S_HINT_H, SDL_Color{4,14,4,200}, SC_BRD_DIM, 1);
    sRowText(renderer, font,
             "Move: Up/Down  Select: A/Left/Right  Page: ZL/ZR",
             x, S_HINT_H, SC_GREY_DIM, -15);
}
