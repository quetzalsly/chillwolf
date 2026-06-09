//
// chill.cpp
// Chocolate Wolfenstein 3D
//
// Optional "chill" quality-of-life overlays.
//

#include "chill.h"

#include <limits.h>
#include <string.h>

chillmode_t ChillMode = chillmode_Off;

const char *ChillModeStrings[chillmode_Count] =
{
    "Off",
    "Pushable tile highlight"
};

static const int CHILL_MESSAGE_TICS = 70;
static const int CHILL_TEXT_X = 4;
static const int CHILL_TEXT_Y = 4;
static const int CHILL_BLUE_R = 0;
static const int CHILL_BLUE_G = 0;
static const int CHILL_BLUE_B = 255;
static const int CHILL_TEXT_R = 255;
static const int CHILL_TEXT_G = 255;
static const int CHILL_TEXT_B = 255;

static longword ChillMessageStartTime = 0;
static uint32_t ChillPaletteSignature = 0;
static boolean ChillColorTablesReady = false;
static byte ChillBlue50Table[256];
static byte ChillWhiteFadeTable[16][256];

static boolean Chill_IsInGameLevel(void)
{
    return ingame && playstate == ex_stillplaying && player != NULL && !gamestate.victoryflag;
}

static uint32_t Chill_GetPaletteSignature(void)
{
    uint32_t signature = 2166136261u;

    for(int i = 0; i < 256; i++)
    {
        signature ^= curpal[i].r;
        signature *= 16777619u;
        signature ^= curpal[i].g;
        signature *= 16777619u;
        signature ^= curpal[i].b;
        signature *= 16777619u;
    }

    return signature;
}

static byte Chill_FindNearestPaletteColor(int red, int green, int blue)
{
    int bestIndex = 0;
    long bestDistance = LONG_MAX;

    for(int i = 0; i < 256; i++)
    {
        long dr = red - curpal[i].r;
        long dg = green - curpal[i].g;
        long db = blue - curpal[i].b;
        long distance = dr * dr + dg * dg + db * db;

        if(distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;

            if(distance == 0)
            {
                break;
            }
        }
    }

    return (byte)bestIndex;
}

static byte Chill_BlendPaletteIndex(byte sourceIndex, int red, int green, int blue, int alpha)
{
    int invAlpha = 255 - alpha;

    int blendedRed = (curpal[sourceIndex].r * invAlpha + red * alpha + 127) / 255;
    int blendedGreen = (curpal[sourceIndex].g * invAlpha + green * alpha + 127) / 255;
    int blendedBlue = (curpal[sourceIndex].b * invAlpha + blue * alpha + 127) / 255;

    return Chill_FindNearestPaletteColor(blendedRed, blendedGreen, blendedBlue);
}

static void Chill_EnsureColorTables(void)
{
    uint32_t signature = Chill_GetPaletteSignature();

    if(ChillColorTablesReady && ChillPaletteSignature == signature)
    {
        return;
    }

    ChillPaletteSignature = signature;
    ChillColorTablesReady = true;

    for(int i = 0; i < 256; i++)
    {
        ChillBlue50Table[i] = Chill_BlendPaletteIndex((byte)i, CHILL_BLUE_R, CHILL_BLUE_G, CHILL_BLUE_B, 128);
    }

    for(int alphaStep = 0; alphaStep < 16; alphaStep++)
    {
        int alpha = (alphaStep * 255) / 15;

        for(int i = 0; i < 256; i++)
        {
            ChillWhiteFadeTable[alphaStep][i] = Chill_BlendPaletteIndex((byte)i, CHILL_TEXT_R, CHILL_TEXT_G, CHILL_TEXT_B, alpha);
        }
    }
}

static void Chill_ShowModeMessage(void)
{
    ChillMessageStartTime = GetTimeCount();
}

boolean Chill_OnKeyPress(int key)
{
    if(key != SDLK_BACKQUOTE)
    {
        return false;
    }

    if(!Chill_IsInGameLevel())
    {
        return false;
    }

    ChillMode = (chillmode_t)((ChillMode + 1) % chillmode_Count);
    Chill_ShowModeMessage();

    return true;
}

boolean Chill_IsPushableTile(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= mapwidth || tiley >= mapheight || mapsegs[1] == NULL)
    {
        return false;
    }

    return *(mapsegs[1] + (tiley << mapshift) + tilex) == PUSHABLETILE;
}

void Chill_HookWorld(byte *vidbuf, unsigned pitch, int screenx, int wallheight, boolean isPushableTile)
{
    if(!Chill_IsInGameLevel() || ChillMode != chillmode_PushableTileHighlight || !isPushableTile || vidbuf == NULL)
    {
        return;
    }

    Chill_EnsureColorTables();

    int halfHeight = wallheight >> 3;

    if(halfHeight <= 0)
    {
        halfHeight = 100;
    }

    int top = viewheight / 2 - halfHeight;
    int bottom = viewheight / 2 + halfHeight - 1;

    if(top < 0)
    {
        top = 0;
    }

    if(bottom >= viewheight)
    {
        bottom = viewheight - 1;
    }

    if(screenx < 0 || screenx >= viewwidth || top > bottom)
    {
        return;
    }

    byte *dest = vidbuf + top * pitch + screenx;

    for(int y = top; y <= bottom; y++)
    {
        *dest = ChillBlue50Table[*dest];
        dest += pitch;
    }
}

static void Chill_DrawFadeText(const char *text, int x, int y, int alpha)
{
    if(text == NULL || !*text || screenBuffer == NULL || alpha <= 0)
    {
        return;
    }

    Chill_EnsureColorTables();

    int alphaStep = (alpha * 15 + 127) / 255;

    if(alphaStep <= 0)
    {
        return;
    }

    if(alphaStep > 15)
    {
        alphaStep = 15;
    }

    fontstruct *font = (fontstruct *)grsegs[STARTFONT + 0];

    if(font == NULL)
    {
        return;
    }

    byte *buffer = VL_LockSurface(screenBuffer);

    if(buffer == NULL)
    {
        return;
    }

    int cursorX = x;
    int height = font->height;

    while(*text)
    {
        byte ch = (byte)*text++;
        int width = font->width[ch];
        int step = width;
        byte *source = ((byte *)font) + font->location[ch];

        for(int sx = 0; sx < width; sx++)
        {
            int destX = cursorX + sx;

            if(destX >= 0 && destX < (int)screenWidth)
            {
                for(int sy = 0; sy < height; sy++)
                {
                    int destY = y + sy;

                    if(destY >= 0 && destY < (int)screenHeight && source[sy * step])
                    {
                        byte *pixel = buffer + destY * screenBuffer->pitch + destX;
                        *pixel = ChillWhiteFadeTable[alphaStep][*pixel];
                    }
                }
            }

            source++;
        }

        cursorX += width;

        if(cursorX >= (int)screenWidth)
        {
            break;
        }
    }

    VL_UnlockSurface(screenBuffer);
}

void Chill_HookOverlay(void)
{
    if(!Chill_IsInGameLevel())
    {
        return;
    }

    longword now = GetTimeCount();
    longword elapsed = now - ChillMessageStartTime;

    if(ChillMessageStartTime == 0 || elapsed >= CHILL_MESSAGE_TICS)
    {
        return;
    }

    int alpha = ((CHILL_MESSAGE_TICS - (int)elapsed) * 255) / CHILL_MESSAGE_TICS;

    Chill_DrawFadeText(ChillModeStrings[ChillMode], CHILL_TEXT_X, CHILL_TEXT_Y, alpha);
}
