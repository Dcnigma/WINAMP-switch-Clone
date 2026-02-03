#include "ui.h"
#include "mp3.h"
#include "player.h"
#include "playlist.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "kiss_fftr.h"

#define FB_W 1920
#define FB_H 1080

#define FFT_SIZE 1024
#define SPECTRUM_BARS 20

static kiss_fftr_cfg fftCfg = NULL;
static kiss_fft_cpx fftOut[FFT_SIZE/2];
static float fftMag[FFT_SIZE/2];
static float bandValues[SPECTRUM_BARS];

static int  scrollOffset = 0;
static int  scrollTimer  = 0;
static int  scrollPause  = 0;
static bool scrollForward = true;
static char lastSongText[256] = {0};

//static int spectrumValues[SPECTRUM_BARS] = {0};
static float spectrumPeaks[SPECTRUM_BARS]  = {0}; // use float for smooth decay
static const float PEAK_FALL_SPEED = 2.0f;        // pixels per frame

extern float g_fftInput[FFT_SIZE];

static void drawPanSlider(SDL_Renderer* renderer,
                          SDL_Texture* texPan,
                          SDL_Rect barRect)
{
    if (!renderer || !texPan) return;

    // --- 1. Get pan (-1.0 → +1.0) ---
    float pan = playerGetPan();
    if (pan < -1.0f) pan = -1.0f;
    if (pan >  1.0f) pan =  1.0f;

    // Convert to 0.0 → 1.0
    float pan01 = (pan + 1.0f) * 0.5f;

    // --- 2. Convert to sprite index (0–27) ---
    const int totalLevels = 28;
    int level = (int)(pan01 * (totalLevels - 1) + 0.5f);

    const int colorStartX = 74;   // start of color strip
    const int colorStartY = 37;
    const int frameW = 27;        // 1718 / 28
    const int frameH = 150;
    const int frameGap = 33;       // set >0 if there is spacing

    if (level < 0) level = 0;
    if (level >= totalLevels) level = totalLevels - 1;

    int srcX = colorStartX + level * (frameW + frameGap);

    SDL_Rect srcBar = { srcX, colorStartY, frameW, frameH };

    // Center bar graphic inside placeholder
    SDL_Rect dstBar = {
        barRect.x + (barRect.w - frameW) / 2,
        barRect.y + (barRect.h - frameH) / 2,
        frameW,
        frameH
    };

    SDL_RenderCopy(renderer, texPan, &srcBar, &dstBar);

    // --- 3. Draw slider knob (VERTICAL movement) ---
    SDL_Rect srcKnob = { 4, 64, 37, 50 };

    int knobTravel = barRect.h - srcKnob.h;

    // 0.0 (LEFT)  -> TOP
    // 0.5 (CENTER)-> MIDDLE
    // 1.0 (RIGHT) -> BOTTOM
    int knobOffset = (int)(pan01 * knobTravel);

    SDL_Rect dstKnob = {
        barRect.x + (barRect.w - srcKnob.w) / 2,
        barRect.y + knobOffset,
        srcKnob.w,
        srcKnob.h
    };

    SDL_RenderCopy(renderer, texPan, &srcKnob, &dstKnob);

}

static void drawVolumeBar(SDL_Renderer* renderer,
                          SDL_Texture* volumeTex,
                          SDL_Rect barRect)
{
    if (!renderer || !volumeTex) return;

    // --- 1. Get volume (0.0 - 1.0) ---
    float volume = playerGetVolume();   // 0.0 = min, 1.0 = max
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    // --- 2. Convert to sprite index (0–27) ---
    const int totalLevels = 28;
    int level = (int)(volume * (totalLevels - 1) + 0.5f);

    const int colorStartX = 76;   // first color frame
    const int frameW = 31;
    const int frameH = 273;
    const int frameGap = 31;      // gap between color frames

    if (level < 0) level = 0;
    if (level >= totalLevels) level = totalLevels - 1;

    // Flip: 0 = bottom (red), max = top (green)
    int srcX = colorStartX + level * (frameW + frameGap);

    SDL_Rect srcBar = { srcX, 0, frameW, frameH };
    SDL_RenderCopy(renderer, volumeTex, &srcBar, &barRect);

    // --- 3. Draw slider knob ---
    SDL_Rect srcKnob = { 4, 66, 38, 52 };

    int knobTravel = barRect.h - srcKnob.h;

    // Loud = bottom, Quiet = top
    int knobOffset = (int)(volume * knobTravel);

    SDL_Rect dstKnob = {
        barRect.x + (barRect.w - srcKnob.w) / 2,
        barRect.y + knobOffset,
        srcKnob.w,
        srcKnob.h
    };

    SDL_RenderCopy(renderer, volumeTex, &srcKnob, &dstKnob);
}



static void drawProgressBar(SDL_Renderer* renderer,
                            SDL_Texture* texProgIndicator,
                            SDL_Rect barRect,
                            SDL_Rect indicatorBaseRect)
{
    if (!renderer || !texProgIndicator) return;

    SDL_Rect dst = indicatorBaseRect;  // default = start position (top)

    if (playerIsPlaying())
    {
        int currentSec = playerGetElapsedSeconds();
        int totalSec   = playerGetTrackLength();

        if (totalSec > 0)
        {
            float progress = (float)currentSec / (float)totalSec;
            if (progress < 0.0f) progress = 0.0f;
            if (progress > 1.0f) progress = 1.0f;

            int travel = barRect.h - indicatorBaseRect.h;
            int offset = (int)(progress * travel);

            dst.y = barRect.y + offset;   // move while playing
        }
    }
    else
    {
        // No song → indicator stays at start (top of bar)
        dst.y = barRect.y;
    }

    SDL_RenderCopy(renderer, texProgIndicator, NULL, &dst);
}

// Draw Mono / Stereo indicators
// Call this from uiRender()
static void drawMonoStereo(SDL_Renderer* renderer, TTF_Font* font, const Mp3MetadataEntry* md, SDL_Rect monoRect,  SDL_Rect stereoRect)
{
    if (!renderer || !font) return;

    // --- Determine which one is active ---
    bool monoActive   = false;
    bool stereoActive = false;

    if (md)
    {
        if (md->channels == 1)
            monoActive = true;
        else if (md->channels == 2)
            stereoActive = true;
    }

    // --- Smooth glow animation using sine wave ---
    static float glowTime = 0.0f;
    glowTime += 0.05f;   // speed of glow (lower = slower pulse)

    float glowWave = (sinf(glowTime) + 1.0f) * 0.5f;  // 0 → 1 smoothly

    // Brightness range (Winamp-ish green pulse)
    Uint8 glowGreen = (Uint8)(130 + 115 * glowWave);   // 140–255

    // --- Mono text ---
    SDL_Color monoColor = {255, 255, 255, 255}; // default white
    if (monoActive)
    {
        monoColor.r = 0;
        monoColor.g = glowGreen;
        monoColor.b = 0;
    }
    drawVerticalText(renderer, font, "Mono", monoRect, monoColor);

    // --- Stereo text ---
    SDL_Color stereoColor = {255, 255, 255, 255}; // default white
    if (stereoActive)
    {
        stereoColor.r = 0;
        stereoColor.g = glowGreen;
        stereoColor.b = 0;
    }
    drawVerticalText(renderer, font, "Stereo", stereoRect, stereoColor);
}




void uiInitFFT()
{
    if (!fftCfg)
        fftCfg = kiss_fftr_alloc(FFT_SIZE, 0, NULL, NULL);
}

static bool fftInitialized = false;


static void computeSpectrum()
{
    kiss_fftr(fftCfg, g_fftInput, fftOut);

    for (int i = 0; i < FFT_SIZE/2; i++)
    {
        float real = fftOut[i].r;
        float imag = fftOut[i].i;
        fftMag[i] = sqrtf(real*real + imag*imag);
    }

    int binsPerBand = (FFT_SIZE/2) / SPECTRUM_BARS;

    for (int b = 0; b < SPECTRUM_BARS; b++)
    {
        float sum = 0;
        for (int j = 0; j < binsPerBand; j++)
        {
            sum += fftMag[b * binsPerBand + j];
        }

        float avg = sum / binsPerBand;

        /* Convert to log scale (more natural for audio) */
        avg = log10f(avg + 1.0f) * 0.6f;   // tweak 0.6 to taste

        if (avg > 1.0f) avg = 1.0f;
        if (avg < 0.0f) avg = 0.0f;

        bandValues[b] = avg;

    }
}

// Call this in uiRender()
static void drawWinampSpectrumVertical(SDL_Renderer* renderer, SDL_Rect rect, const float* bands)
{
    if (!renderer || !bands) return;

    int barHeight  = rect.h / SPECTRUM_BARS;
    int barSpacing = 1;

    for (int i = 0; i < SPECTRUM_BARS; i++)
    {
        // Flip frequency order (bass at top)
        int bandIndex = SPECTRUM_BARS - 1 - i;

        float value = bands[bandIndex];
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;

        int filledWidth = (int)(value * rect.w);

        int y = rect.y + rect.h - (i + 1) * barHeight;

        /* ---- Draw colored bar with gradient ---- */
        for (int x = 0; x < filledWidth; x++)
        {
            float t = (float)x / rect.w;   // 0 = quiet side, 1 = loud edge

            SDL_Color color;

            if (t < 0.75f)   // mostly green zone
            {
                float k = t / 0.75f;
                color.r = (Uint8)(k * 255);
                color.g = 255;
                color.b = 0;
            }
            else             // red only near the peak
            {
                float k = (t - 0.75f) / 0.25f;
                color.r = 255;
                color.g = (Uint8)((1.0f - k) * 255);
                color.b = 0;
            }

            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);

            SDL_Rect slice = { rect.x + x, y, 1, barHeight - barSpacing };
            SDL_RenderFillRect(renderer, &slice);
        }

        /* ---- Peak cap handling ---- */
        float targetPeak = value * rect.w;

        if (targetPeak > spectrumPeaks[i])
            spectrumPeaks[i] = targetPeak;
        else
        {
            spectrumPeaks[i] -= PEAK_FALL_SPEED;
            if (spectrumPeaks[i] < 0) spectrumPeaks[i] = 0;
        }

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

        SDL_Rect peakRect = {
            rect.x + (int)spectrumPeaks[i],
            y,
            2,
            barHeight - barSpacing
        };

        SDL_RenderFillRect(renderer, &peakRect);
    }
}



static void getScrollingText(const char* fullText, char* out, size_t outSize)
{
    const int visibleChars   = 37;
    const int scrollSpeed    = 8;    // lower = faster
    const int pauseDuration  = 60;   // frames to pause at ends (~1 sec at 60fps)

    int len = strlen(fullText);

    // --- Reset scroll if song changed ---
    if (strcmp(fullText, lastSongText) != 0)
    {
        strncpy(lastSongText, fullText, sizeof(lastSongText)-1);
        scrollOffset  = 0;
        scrollTimer   = 0;
        scrollPause   = pauseDuration; // pause before first movement
        scrollForward = true;
    }

    // If text fits, no scrolling needed
    if (len <= visibleChars)
    {
        snprintf(out, outSize, "%-*s", visibleChars, fullText); // pad to fill area
        return;
    }

    // Handle pause at edges
    if (scrollPause > 0)
    {
        scrollPause--;
    }
    else
    {
        scrollTimer++;
        if (scrollTimer >= scrollSpeed)
        {
            scrollTimer = 0;

            if (scrollForward)
                scrollOffset++;
            else
                scrollOffset--;

            // Hit right end
            if (scrollOffset >= len - visibleChars)
            {
                scrollOffset = len - visibleChars;
                scrollForward = false;
                scrollPause = pauseDuration;
            }
            // Hit left end
            else if (scrollOffset <= 0)
            {
                scrollOffset = 0;
                scrollForward = true;
                scrollPause = pauseDuration;
            }
        }
    }

    // Copy visible window
    snprintf(out, outSize, "%.*s", visibleChars, fullText + scrollOffset);
}


// --- Draw filled rectangle with alpha ---
void drawRect(SDL_Renderer* renderer, SDL_Rect r, Uint8 rC, Uint8 gC, Uint8 bC, Uint8 aC)
{
    SDL_SetRenderDrawColor(renderer, rC, gC, bC, aC);
    SDL_RenderFillRect(renderer, &r);
}

void formatTimeLong(int seconds, char* out, size_t outSize)
{
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    if (h > 0)
        snprintf(out, outSize, "%02d:%02d:%02d", h, m, s);
    else
        snprintf(out, outSize, "%02d:%02d", m, s);
}


void drawVerticalText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      const char* text,
                      SDL_Rect rect,
                      SDL_Color color,
                      int paddingX,          // manual horizontal offset
                      int paddingY,          // manual vertical offset
                      VerticalAlign alignY)  // vertical alignment inside rect
{
    if (!font || !text) return;

    SDL_Surface* s = TTF_RenderText_Solid(font, text, color);
    if (!s) return;

    SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
    if (!t) { SDL_FreeSurface(s); return; }

    SDL_Rect dst;
    dst.w = s->w;
    dst.h = s->h;

    // Horizontal position (after rotation)
    dst.x = rect.x + rect.w - dst.h + paddingX;

    // Vertical alignment inside the UI rectangle
    switch (alignY)
    {
        case ALIGN_TOP:
            dst.y = rect.y + paddingY;
            break;

        case ALIGN_CENTER:
            dst.y = rect.y + (rect.h - dst.w) / 2 + paddingY;
            break;

        case ALIGN_BOTTOM:
            dst.y = rect.y + rect.h - dst.w - paddingY;
            break;
    }

    SDL_Point center = {0, 0};
    SDL_RenderCopyEx(renderer, t, NULL, &dst, 90.0, &center, SDL_FLIP_NONE);

    SDL_DestroyTexture(t);
    SDL_FreeSurface(s);
}

// Backward-compatible wrapper (old calls still work)
void drawVerticalText(SDL_Renderer* renderer,
                      TTF_Font* font,
                      const char* text,
                      SDL_Rect rect,
                      SDL_Color color)
{
    drawVerticalText(renderer, font, text, rect, color,
                     16,          // default paddingX
                     0,           // default paddingY
                     ALIGN_TOP);  // default alignment
}

void formatTime(int seconds, char* out, size_t size)
{
    int m = seconds / 60;
    int s = seconds % 60;
    snprintf(out, size, "%02d:%02d", m, s);
}

static void drawPlaylistSlider(SDL_Renderer* renderer,
                               SDL_Texture* knobTex)
{
    if (!renderer || !knobTex) return;

    int totalItems = playlistGetCount();
    int scroll     = playlistGetScroll();
    int visible    = playlistGetMaxVisible();

    if (totalItems <= 0) return;

    // --- Slider track area (where knob can move) ---
    const int trackX = 208;
    const int trackY = 1020;
    const int trackW = 326;
    const int trackH = 30;

    // --- Knob size ---
    const int knobW = 103;
    const int knobH = 30;

    // If everything fits on screen, knob stays RIGHT
    float t = 1.0f;

    int maxScroll = totalItems - visible;
    if (maxScroll > 0)
    {
        t = 1.0f - ((float)scroll / (float)maxScroll);
    }

    // Clamp
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    int travel = trackW - knobW;
    int knobX  = trackX + (int)(t * travel);
    int knobY  = trackY;

    SDL_Rect dstKnob = { knobX, knobY, knobW, knobH };

    SDL_RenderCopy(renderer, knobTex, NULL, &dstKnob);
}


// --- Render full UI ---
void uiRender(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* fontBig, SDL_Texture* skin, SDL_Texture* texProgIndicator, SDL_Texture* texVolume, SDL_Texture* texPan,  SDL_Texture* texPlaylistKnob, const char* songText)
{
  if (!fftInitialized)
  {
      uiInitFFT();
      fftInitialized = true;
  }

    // --- Live playtime string ---
    char liveTime[16];
    int elapsed = playerGetElapsedSeconds();
    snprintf(liveTime, sizeof(liveTime), "%02d:%02d", elapsed / 60, elapsed % 60);

    // Clear screen
    SDL_RenderClear(renderer);

    // Draw skin background
    if (skin)
    {
        SDL_Rect dst = {0,0,FB_W,FB_H};
        SDL_RenderCopy(renderer, skin, NULL, &dst);
    }

    // --- UI RECTANGLES ---
//    SDL_Rect topBar        = {1837,   0,  83,1080};
//    SDL_Rect mainPlayer    = {1287,   0, 558,1080};
    SDL_Rect eqSection     = {650,    0, 654,1080};
//    SDL_Rect playlist      = {0,      0, 636,1080};


    SDL_Rect progBar        = {1467,  64,  61, 982};
    SDL_Rect progIndicatorR = {1469,  577, 62, 113};

    drawProgressBar(renderer, texProgIndicator, progBar, progIndicatorR);

    SDL_Rect songInfo      = {1721, 429, 68, 614};
    SDL_Rect playtimeInfo  = {1694, 178,100,221};
    SDL_Rect kbpsInfo      = {1639, 426, 57, 74};
    SDL_Rect kHzInfo       = {1639, 600, 57, 55};
//    SDL_Rect playlistFiles = {215,   50,310,950};

    SDL_Rect prevButton    = {1340,  60,100, 90};
    SDL_Rect playButton    = {1340, 151,100, 90};
    SDL_Rect pauseButton   = {1340, 242,100, 90};
    SDL_Rect stopButton    = {1340, 332,100, 90};
    SDL_Rect nextButton    = {1340, 426,100, 90};
    SDL_Rect ejectButton   = {1340, 532,100, 90};
    SDL_Rect shuffleButton = {1357, 642, 72,182};
    SDL_Rect repeatButton  = {1357, 825, 72,115};

    SDL_Rect monoRect   = {1670, 835, 30, 90};
    SDL_Rect stereoRect = {1670, 940, 30, 90};

    const Mp3MetadataEntry* md = mp3GetTrackMetadata(playerGetCurrentTrackIndex());
    drawMonoStereo(renderer, font, md, monoRect, stereoRect);

    drawPlaylistSlider(renderer, texPlaylistKnob);


    SDL_Rect eqBand1       = {720,   91,346, 33};
    SDL_Rect eqBand2       = {720,  315,346, 33};
    SDL_Rect eqBand3       = {720,  387,346, 33};
    SDL_Rect eqBand4       = {720,  457,346, 33};
    SDL_Rect eqBand5       = {720,  532,346, 33};
    SDL_Rect eqBand6       = {720,  603,346, 33};
    SDL_Rect eqBand7       = {720,  671,346, 33};
    SDL_Rect eqBand8       = {720,  743,346, 33};
    SDL_Rect eqBand9       = {720,  813,346, 33};
    SDL_Rect eqBand10      = {720,  885,346, 33};
    SDL_Rect eqBand11      = {720,  953,346, 33};

    SDL_Rect eqPreset1     = {1112,  53, 73,104};
    SDL_Rect eqPreset2     = {1112, 153, 73,131};
    SDL_Rect eqPreset3     = {1112, 851, 73,170};

//    SDL_Rect volumeSlider  = {1551, 421,40,264};

    SDL_Rect volumeBarRect = { 1551, 421, 40, 264 }; // adjust to your skin

    drawVolumeBar(renderer, texVolume, volumeBarRect);


    SDL_Rect panSlider     = {1552, 698,40,145};

    SDL_Rect addPlaylist   = {70,  42,100,100};
    SDL_Rect rmPlaylist    = {70, 158,100,100};
    SDL_Rect selPlaylist   = {70, 270,100,100};
    SDL_Rect miscPlaylist  = {70, 383,100,100};
    SDL_Rect ListOptions   = {70, 893,100,100};
    SDL_Rect Duration      = {45, 765,50,100};
    SDL_Rect TotPylDurat   = {109, 512,63, 358};

    // --- Draw rectangles with transparency ---
//    drawRect(renderer, topBar, 200,0,0,150);
//    drawRect(renderer, mainPlayer, 0,100,200,150);
    drawRect(renderer, eqSection, 0,200,100,150);
//    drawRect(renderer, playlist, 200,200,0,150);
//    drawRect(renderer, progBar, 150,0,200,150);
//    drawRect(renderer, songInfo, 100,100,100,150);
//    drawRect(renderer, playtimeInfo, 0,200,100,150);
//    drawRect(renderer, kbpsInfo, 200,200,0,150);
//    drawRect(renderer, kHzInfo, 150,0,200,150);
//    drawRect(renderer, playlistFiles, 100,100,100,150);

    drawRect(renderer, prevButton, 255,0,0,150);
    drawRect(renderer, nextButton, 0,255,0,150);
    drawRect(renderer, playButton, 255,0,0,150);
    drawRect(renderer, stopButton, 0,255,0,150);
    drawRect(renderer, pauseButton, 255,0,0,150);
    drawRect(renderer, ejectButton, 0,255,0,150);
    drawRect(renderer, shuffleButton, 255,0,0,150);
    drawRect(renderer, repeatButton, 0,255,0,150);

    drawRect(renderer, eqBand1, 0,0,255,150);
    drawRect(renderer, eqBand2, 0,255,255,150);
    drawRect(renderer, eqBand3, 255,0,255,150);
    drawRect(renderer, eqBand4, 255,255,0,150);
    drawRect(renderer, eqBand5, 0,255,255,150);
    drawRect(renderer, eqBand6, 255,0,255,150);
    drawRect(renderer, eqBand7, 255,255,0,150);
    drawRect(renderer, eqBand8, 100,100,200,150);
    drawRect(renderer, eqBand9, 0,255,255,150);
    drawRect(renderer, eqBand10, 255,0,255,150);
    drawRect(renderer, eqBand11, 100,100,200,150);

    drawRect(renderer, eqPreset1, 200,100,100,150);
    drawRect(renderer, eqPreset2, 100,200,100,150);
    drawRect(renderer, eqPreset3, 100,100,100,150);

//    drawRect(renderer, volumeSlider, 150,150,0,150);
//    drawRect(renderer, panSlider, 0,150,150,150);

    drawPanSlider(renderer, texPan, panSlider);

    drawRect(renderer, addPlaylist, 150,150,0,150);
    drawRect(renderer, rmPlaylist, 0,150,150,150);
    drawRect(renderer, selPlaylist, 150,150,0,150);
    drawRect(renderer, miscPlaylist, 0,150,150,150);
    drawRect(renderer, ListOptions, 150,150,0,150);
//    drawRect(renderer, Duration, 150,150,0,150);
//    drawRect(renderer, TotPylDurat, 255,0,0,150);

    // Example rectangle in your UI
//    SDL_Rect spectrumRect = {1592, 92, 93, 306};
//    drawWinampSpectrumVertical(renderer, spectrumRect);

    computeSpectrum();

    SDL_Rect spectrumRect = {1592, 90, 93, 306};
    drawWinampSpectrumVertical(renderer, spectrumRect, bandValues);


    SDL_Color green = {0, 255, 0, 255};
//    SDL_Color white = {0, 255, 255, 255};

    char scrollingSong[64];
    getScrollingText(songText, scrollingSong, sizeof(scrollingSong));



    drawVerticalText(renderer, font, scrollingSong, songInfo, green,
                     16,
                     10,
                     ALIGN_TOP);


     drawVerticalText(renderer, fontBig, liveTime, playtimeInfo, green,
                      85,      // horizontal push
                      0,
                      ALIGN_CENTER);

    int playing = playerGetCurrentTrackIndex();
    if (playing >= 0)
    {
        const Mp3MetadataEntry* md = mp3GetTrackMetadata(playing);
        if (md)
        {
            char kbpsText[8];
            char kHzText[8];

            snprintf(kbpsText, sizeof(kbpsText), "%d", md->bitrateKbps);
            snprintf(kHzText, sizeof(kHzText), "%d", md->sampleRateKHz);

            // --- Subtle glow animation ---
            static float infoGlowTime = 0.0f;
            infoGlowTime += 0.04f;  // slower than mono/stereo

            float glowWave = (sinf(infoGlowTime) + 1.0f) * 0.5f;  // 0–1

            Uint8 greenLevel = (Uint8)(180 + 75 * glowWave); // soft range: 180–255

            SDL_Color infoGreen = {0, greenLevel, 0, 255};

            drawVerticalText(renderer, font, kbpsText, kbpsInfo, infoGreen,
                             20, 10, ALIGN_TOP);

            drawVerticalText(renderer, font, kHzText, kHzInfo, infoGreen,
                             20, 10, ALIGN_TOP);
        }
    }



     drawVerticalText(renderer, font, liveTime, Duration, green,
                 25,      // small horizontal padding
                 10,      // small top padding
                 ALIGN_TOP);

     // --- Compute elapsed seconds so far in playlist ---
     int playlistElapsed = 0;
     int currentTrack = playerGetCurrentIndex();
     for (int i = 0; i < currentTrack; i++)
     {
         const Mp3MetadataEntry* md = mp3GetTrackMetadata(i);
         if (md) playlistElapsed += md->durationSeconds;
     }

     // Add current track elapsed
     playlistElapsed += playerGetElapsedSeconds();

     // --- Compute total playlist duration ---
     int playlistTotal = 0;
     int trackCount = mp3GetPlaylistCount();
     for (int i = 0; i < trackCount; i++)
     {
         const Mp3MetadataEntry* md = mp3GetTrackMetadata(i);
         if (md) playlistTotal += md->durationSeconds;
     }

     // --- Format strings ---
     char elapsedStr[32];
     char totalStr[32];
     char playlistTime[64];

     formatTimeLong(playlistElapsed, elapsedStr, sizeof(elapsedStr));
     formatTimeLong(playlistTotal, totalStr, sizeof(totalStr));
     snprintf(playlistTime, sizeof(playlistTime), "%s / %s", elapsedStr, totalStr);

     // --- Draw in UI ---

     drawVerticalText(renderer, font, playlistTime, TotPylDurat, green,
                      25,      // horizontal padding
                      10,      // top padding
                      ALIGN_TOP);


    // --- Playlist ---
    renderPlaylist(renderer, font);

  //  SDL_RenderPresent(renderer);
}
