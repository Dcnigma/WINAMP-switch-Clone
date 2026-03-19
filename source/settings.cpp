#include "settings.h"
#include "settings_state.h"

static bool g_settingsOpen = false;
static int  g_selectedItem = 0;

PlayerSettings g_settings =
{
    false,   // crossfadeEnabled
    3.0f     // crossfadeSeconds
};

void settingsOpen()
{
    g_settingsOpen = true;
}

void settingsClose()
{
    g_settingsOpen = false;
}

bool settingsIsOpen()
{
    return g_settingsOpen;
}


void settingsHandleInput(PadState* pad)
{
    u64 down = padGetButtonsDown(pad);

    if (down & HidNpadButton_B)
    {
        settingsClose();
        return;
    }

    if (down & HidNpadButton_Up)
    {
        g_selectedItem--;
        if (g_selectedItem < 0)
            g_selectedItem = SETTINGS_COUNT - 1;
    }

    if (down & HidNpadButton_Down)
    {
        g_selectedItem++;
        if (g_selectedItem >= SETTINGS_COUNT)
            g_selectedItem = 0;
    }

    if (down & HidNpadButton_A)
    {
        switch (g_selectedItem)
        {
            case SETTING_CROSSFADE:
                g_settings.crossfadeEnabled =
                    !g_settings.crossfadeEnabled;
                break;
            case SETTING_CROSSFADE_TIME:
                // Optional: reset to default
                g_settings.crossfadeSeconds = 3.0f;
                break;
            case SETTING_BACK:
                settingsClose();
                break;
        }
    }

    if (down & HidNpadButton_Left ||
        down & HidNpadButton_Right)
    {
        if (g_selectedItem == SETTING_CROSSFADE_TIME)
        {
            float step = 0.5f;

            if (down & HidNpadButton_Left)
                g_settings.crossfadeSeconds -= step;
            else
                g_settings.crossfadeSeconds += step;

            if (g_settings.crossfadeSeconds < 0.5f)
                g_settings.crossfadeSeconds = 0.5f;

            if (g_settings.crossfadeSeconds > 10.0f)
                g_settings.crossfadeSeconds = 10.0f;
        }
    }
}

void settingsRender(SDL_Renderer* renderer, TTF_Font* font)
{
    if (!g_settingsOpen)
        return;

    SDL_Rect panel =
    {
        610, 90,
        700, 900
    };

    SDL_SetRenderDrawColor(renderer, 20,20,20,230);
    SDL_RenderFillRect(renderer, &panel);

    const char* items[SETTINGS_COUNT] =
    {
        "Crossfade",
        "Crossfade Time",
        "Auto EQ",
        "Back"
    };

    for (int i = 0; i < SETTINGS_COUNT; i++)
    {
        SDL_Color col =
            (i == g_selectedItem)
            ? SDL_Color{255,255,0,255}
            : SDL_Color{255,255,255,255};

        int y = 180 + i * 120;

        // 🔹 LEFT SIDE (label)
        SDL_Surface* surf =
            TTF_RenderUTF8_Blended(font, items[i], col);

        SDL_Texture* tex =
            SDL_CreateTextureFromSurface(renderer, surf);

        SDL_Rect dst =
        {
            700,
            y,
            surf->w,
            surf->h
        };
        if (i == SETTING_CROSSFADE_TIME)
        {
            int barX = 950;
            int barY = y + 40;
            int barW = 250;
            int barH = 10;

            // background
            SDL_SetRenderDrawColor(renderer, 80,80,80,255);
            SDL_Rect bg = { barX, barY, barW, barH };
            SDL_RenderFillRect(renderer, &bg);

            // fill
            float t = (g_settings.crossfadeSeconds - 0.5f) / (10.0f - 0.5f);

            SDL_SetRenderDrawColor(renderer, 0,200,255,255);
            SDL_Rect fill = { barX, barY, (int)(barW * t), barH };
            SDL_RenderFillRect(renderer, &fill);
        }
        SDL_RenderCopy(renderer, tex, NULL, &dst);

        SDL_FreeSurface(surf);
        SDL_DestroyTexture(tex);

        // 🔹 RIGHT SIDE (value)
        char valueText[64];
        valueText[0] = '\0';

        if (i == SETTING_CROSSFADE)
        {
            sprintf(valueText, "%s",
                g_settings.crossfadeEnabled ? "ON" : "OFF");
        }
        else if (i == SETTING_CROSSFADE_TIME)
        {
            sprintf(valueText, "%.1fs",
                g_settings.crossfadeSeconds);
        }

        if (valueText[0])
        {
            SDL_Surface* vs =
                TTF_RenderUTF8_Blended(font, valueText, col);

            SDL_Texture* vt =
                SDL_CreateTextureFromSurface(renderer, vs);

            SDL_Rect vdst =
            {
                1100, // right aligned
                y,
                vs->w,
                vs->h
            };

            SDL_RenderCopy(renderer, vt, NULL, &vdst);

            SDL_FreeSurface(vs);
            SDL_DestroyTexture(vt);
        }
    }
}
