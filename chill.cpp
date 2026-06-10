//
// chill.cpp
// Chocolate Wolfenstein 3D
//
// Optional quality-of-life overlays.
//

#include "chill.h"
#include "pathfind.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

highlightmode_t HighlightMode = highlightmode_Off;
pathfindmode_t PathfindMode = pathfindmode_Off;

const char *HighlightModeStrings[highlightmode_Count] =
{
    "Off",
    "Pushable tile highlight"
};

const char *PathfindModeStrings[pathfindmode_Count] =
{
    "Off",
    "Closest pushable tile",
    "Any%",
    "Secret exit",
    "100%"
};

static longword ChillMessageStartTime = 0;
static char ChillMessageText[80];

static uint32_t ChillPaletteSignature = 0;
static boolean ChillColorTablesReady = false;
static byte ChillPushableHighlightTable[256];
static byte ChillPathGlowTable[256];
static byte ChillPathTrailTable[256];
static byte ChillPathArrowTable[256];
static byte ChillPathArrowHotTable[256];
static byte ChillWhiteFadeTable[16][256];

static ChillPathPoint ChillPath[CHILL_PATHFIND_MAX_PATH];
static fixed ChillPathWorldX[CHILL_PATH_MAX_DRAW_POINTS];
static fixed ChillPathWorldY[CHILL_PATH_MAX_DRAW_POINTS];
static int ChillPathCumulativeDistance[CHILL_PATH_MAX_DRAW_POINTS];

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
        ChillPushableHighlightTable[i] = Chill_BlendPaletteIndex
        (
            (byte)i,
            CHILL_PUSHABLE_HIGHLIGHT_R,
            CHILL_PUSHABLE_HIGHLIGHT_G,
            CHILL_PUSHABLE_HIGHLIGHT_B,
            CHILL_PUSHABLE_HIGHLIGHT_ALPHA
        );

        ChillPathGlowTable[i] = Chill_BlendPaletteIndex
        (
            (byte)i,
            CHILL_PATH_GLOW_R,
            CHILL_PATH_GLOW_G,
            CHILL_PATH_GLOW_B,
            CHILL_PATH_GLOW_ALPHA
        );

        ChillPathTrailTable[i] = Chill_BlendPaletteIndex
        (
            (byte)i,
            CHILL_PATH_TRAIL_R,
            CHILL_PATH_TRAIL_G,
            CHILL_PATH_TRAIL_B,
            CHILL_PATH_TRAIL_ALPHA
        );

        ChillPathArrowTable[i] = Chill_BlendPaletteIndex
        (
            (byte)i,
            CHILL_PATH_ARROW_R,
            CHILL_PATH_ARROW_G,
            CHILL_PATH_ARROW_B,
            CHILL_PATH_ARROW_ALPHA
        );

        ChillPathArrowHotTable[i] = Chill_BlendPaletteIndex
        (
            (byte)i,
            CHILL_PATH_ARROW_HOT_R,
            CHILL_PATH_ARROW_HOT_G,
            CHILL_PATH_ARROW_HOT_B,
            CHILL_PATH_ARROW_HOT_ALPHA
        );
    }

    for(int alphaStep = 0; alphaStep < 16; alphaStep++)
    {
        int alpha = (alphaStep * CHILL_TEXT_ALPHA) / 15;

        for(int i = 0; i < 256; i++)
        {
            ChillWhiteFadeTable[alphaStep][i] = Chill_BlendPaletteIndex
            (
                (byte)i,
                CHILL_TEXT_R,
                CHILL_TEXT_G,
                CHILL_TEXT_B,
                alpha
            );
        }
    }
}

static void Chill_ShowModeMessage(const char *prefix, const char *modeName)
{
    snprintf(ChillMessageText, sizeof(ChillMessageText), "%s: %s", prefix, modeName);
    ChillMessageText[sizeof(ChillMessageText) - 1] = 0;
    ChillMessageStartTime = GetTimeCount();
}

boolean Chill_OnKeyPress(int key)
{
    if(!Chill_IsInGameLevel())
    {
        return false;
    }

    if(key == CHILL_HIGHLIGHT_MODE_KEY)
    {
        HighlightMode = (highlightmode_t)((HighlightMode + 1) % highlightmode_Count);
        Chill_ShowModeMessage("Highlight mode", HighlightModeStrings[HighlightMode]);
        return true;
    }

    if(key == CHILL_PATHFIND_MODE_KEY)
    {
        PathfindMode = (pathfindmode_t)((PathfindMode + 1) % pathfindmode_Count);
        Chill_ShowModeMessage("Pathfind mode", PathfindModeStrings[PathfindMode]);
        return true;
    }

    return false;
}

boolean Chill_IsPushableTile(int tilex, int tiley)
{
    return ChillPathfinder::IsPushableTile(tilex, tiley);
}

void Chill_HookWallPost(byte *vidbuf, unsigned pitch, int screenx, int wallheight, boolean isPushableTile)
{
    if(!Chill_IsInGameLevel() || HighlightMode != highlightmode_PushableTileHighlight || !isPushableTile || vidbuf == NULL)
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
        *dest = ChillPushableHighlightTable[*dest];
        dest += pitch;
    }
}

static int Chill_ApproxDistance(fixed dx, fixed dy)
{
    int adx = ABS(dx);
    int ady = ABS(dy);

    if(adx > ady)
    {
        return adx + (ady >> 1);
    }

    return ady + (adx >> 1);
}

static int Chill_GetPositiveModulo(int value, int modulo)
{
    int result = value % modulo;

    if(result < 0)
    {
        result += modulo;
    }

    return result;
}

static int Chill_BuildWorldPath(int pathLength)
{
    if(pathLength < 2)
    {
        return 0;
    }

    int drawLength = 0;
    int lastDx = 0;
    int lastDy = 0;

    for(int i = 0; i < pathLength; i++)
    {
        boolean addPoint = false;

        if(i == 0 || i == pathLength - 1)
        {
            addPoint = true;
        }
        else
        {
            int dx = ChillPath[i + 1].tilex - ChillPath[i].tilex;
            int dy = ChillPath[i + 1].tiley - ChillPath[i].tiley;

            if(i == 1)
            {
                lastDx = ChillPath[i].tilex - ChillPath[i - 1].tilex;
                lastDy = ChillPath[i].tiley - ChillPath[i - 1].tiley;
            }

            if(dx != lastDx || dy != lastDy)
            {
                addPoint = true;
            }

            lastDx = dx;
            lastDy = dy;
        }

        if(addPoint)
        {
            if(drawLength >= CHILL_PATH_MAX_DRAW_POINTS)
            {
                break;
            }

            ChillPathWorldX[drawLength] = ((fixed)ChillPath[i].tilex << TILESHIFT) + (TILEGLOBAL >> 1);
            ChillPathWorldY[drawLength] = ((fixed)ChillPath[i].tiley << TILESHIFT) + (TILEGLOBAL >> 1);
            ChillPathCumulativeDistance[drawLength] = 0;
            drawLength++;
        }
    }

    if(drawLength < 2)
    {
        return 0;
    }

    // Make the path feel attached to the player instead of the center of the
    // player's current tile.
    ChillPathWorldX[0] = player->x;
    ChillPathWorldY[0] = player->y;

    for(int i = 1; i < drawLength; i++)
    {
        fixed dx = ChillPathWorldX[i] - ChillPathWorldX[i - 1];
        fixed dy = ChillPathWorldY[i] - ChillPathWorldY[i - 1];
        ChillPathCumulativeDistance[i] = ChillPathCumulativeDistance[i - 1] + Chill_ApproxDistance(dx, dy);
    }

    return drawLength;
}

static boolean Chill_ScreenFloorPixelToWorld(int screenx, int screeny, fixed *worldx, fixed *worldy)
{
    int horizon = viewheight / 2;
    int row = screeny - horizon;

    if(row <= 0 || worldx == NULL || worldy == NULL || scale == 0)
    {
        return false;
    }

    fixed forward = (fixed)(((int64_t)heightnumerator << 8) / ((int64_t)row * 16));

    if(forward < MINDIST)
    {
        return false;
    }

    fixed side = (fixed)(((int64_t)(screenx - centerx) * forward) / scale);

    fixed gx = FixedMul(forward, viewcos) + FixedMul(side, viewsin);
    fixed gy = -FixedMul(forward, viewsin) + FixedMul(side, viewcos);

    *worldx = viewx + gx;
    *worldy = viewy + gy;

    return true;
}

static boolean Chill_IsWorldFloorVisible(fixed worldx, fixed worldy)
{
    int tilex = worldx >> TILESHIFT;
    int tiley = worldy >> TILESHIFT;

    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE)
    {
        return false;
    }

    return spotvis[tilex][tiley] ? true : false;
}

static int Chill_EvaluatePathPixel(fixed worldx, fixed worldy, int pathLength, int animationPhase)
{
    int bestSegment = -1;
    int bestT = 0;
    int bestSegmentLength = 0;
    int64_t bestDistanceSquared = (int64_t)CHILL_PATH_GLOW_HALF_WIDTH_GLOBAL * CHILL_PATH_GLOW_HALF_WIDTH_GLOBAL + 1;

    for(int i = 0; i < pathLength - 1; i++)
    {
        fixed ax = ChillPathWorldX[i];
        fixed ay = ChillPathWorldY[i];
        fixed bx = ChillPathWorldX[i + 1];
        fixed by = ChillPathWorldY[i + 1];
        fixed dx = bx - ax;
        fixed dy = by - ay;
        int64_t lengthSquared = (int64_t)dx * dx + (int64_t)dy * dy;

        if(lengthSquared <= 0)
        {
            continue;
        }

        int64_t dot = (int64_t)(worldx - ax) * dx + (int64_t)(worldy - ay) * dy;
        int t = (int)((dot * 1024) / lengthSquared);

        if(t < 0)
        {
            t = 0;
        }
        else if(t > 1024)
        {
            t = 1024;
        }

        fixed nearestX = ax + (fixed)(((int64_t)dx * t) / 1024);
        fixed nearestY = ay + (fixed)(((int64_t)dy * t) / 1024);
        fixed ndx = worldx - nearestX;
        fixed ndy = worldy - nearestY;
        int64_t distanceSquared = (int64_t)ndx * ndx + (int64_t)ndy * ndy;

        if(distanceSquared < bestDistanceSquared)
        {
            bestDistanceSquared = distanceSquared;
            bestSegment = i;
            bestT = t;
            bestSegmentLength = Chill_ApproxDistance(dx, dy);
        }
    }

    if(bestSegment < 0)
    {
        return 0;
    }

    int64_t trailHalfWidthSquared = (int64_t)CHILL_PATH_TRAIL_HALF_WIDTH_GLOBAL * CHILL_PATH_TRAIL_HALF_WIDTH_GLOBAL;
    int64_t arrowHalfWidthSquared = (int64_t)CHILL_PATH_ARROW_HALF_WIDTH_GLOBAL * CHILL_PATH_ARROW_HALF_WIDTH_GLOBAL;

    int level = 1;

    if(bestDistanceSquared <= trailHalfWidthSquared)
    {
        level = 2;
    }

    int along = ChillPathCumulativeDistance[bestSegment] + (int)(((int64_t)bestSegmentLength * bestT) / 1024);
    int localArrow = Chill_GetPositiveModulo(along - animationPhase, CHILL_PATH_ARROW_SPACING_GLOBAL);

    if(localArrow < CHILL_PATH_ARROW_LENGTH_GLOBAL)
    {
        // localArrow runs from the rear of the animated arrow toward its nose.
        // Keep the tail wide and taper down toward the destination so the
        // triangle points in the same direction as the path.
        int distanceToTip = CHILL_PATH_ARROW_LENGTH_GLOBAL - localArrow;
        int arrowHalfWidth = (CHILL_PATH_ARROW_HALF_WIDTH_GLOBAL * distanceToTip) / CHILL_PATH_ARROW_LENGTH_GLOBAL;

        if(arrowHalfWidth < CHILL_PATH_TRAIL_HALF_WIDTH_GLOBAL)
        {
            arrowHalfWidth = CHILL_PATH_TRAIL_HALF_WIDTH_GLOBAL;
        }

        int64_t arrowWidthSquared = (int64_t)arrowHalfWidth * arrowHalfWidth;

        if(bestDistanceSquared <= arrowWidthSquared && bestDistanceSquared <= arrowHalfWidthSquared)
        {
            level = 3;

            if(localArrow > (CHILL_PATH_ARROW_LENGTH_GLOBAL * 4) / 5 && bestDistanceSquared <= trailHalfWidthSquared)
            {
                level = 4;
            }
        }
    }

    return level;
}

static void Chill_ApplyPathPixel(byte *pixel, int level)
{
    switch(level)
    {
        case 1:
            *pixel = ChillPathGlowTable[*pixel];
            break;

        case 2:
            *pixel = ChillPathTrailTable[*pixel];
            break;

        case 3:
            *pixel = ChillPathArrowTable[*pixel];
            break;

        case 4:
            *pixel = ChillPathArrowHotTable[*pixel];
            break;

        default:
            break;
    }
}

static int Chill_GetWallFloorStartY(int screenx)
{
    int floorStartY = viewheight / 2 + (wallheight[screenx] >> 4) + 1;

    if(floorStartY < viewheight / 2 + 1)
    {
        floorStartY = viewheight / 2 + 1;
    }

    if(floorStartY > viewheight)
    {
        floorStartY = viewheight;
    }

    return floorStartY;
}

static void Chill_DrawFloorRasterPath(byte *vidbuf, unsigned pitch, int pathLength)
{
    int animationPhase = (int)((GetTimeCount() * CHILL_PATH_ARROW_SPEED_GLOBAL_PER_TIC) % CHILL_PATH_ARROW_SPACING_GLOBAL);
    int horizon = viewheight / 2;

    for(int y = horizon + 1; y < viewheight; y++)
    {
        byte *row = vidbuf + y * pitch;

        for(int x = 0; x < viewwidth; x++)
        {
            if(y < Chill_GetWallFloorStartY(x))
            {
                continue;
            }

            fixed worldx;
            fixed worldy;

            if(!Chill_ScreenFloorPixelToWorld(x, y, &worldx, &worldy))
            {
                continue;
            }

            if(!Chill_IsWorldFloorVisible(worldx, worldy))
            {
                continue;
            }

            int level = Chill_EvaluatePathPixel(worldx, worldy, pathLength, animationPhase);

            if(level)
            {
                Chill_ApplyPathPixel(&row[x], level);
            }
        }
    }
}

static boolean Chill_FindPathForCurrentMode(int *pathLength, int *targetX, int *targetY)
{
    switch(PathfindMode)
    {
        case pathfindmode_ClosestPushableTile:
            return ChillPathfinder::FindClosestPushableTile(ChillPath, CHILL_PATHFIND_MAX_PATH, pathLength, targetX, targetY);

        case pathfindmode_AnyPercent:
            return ChillPathfinder::FindStandardExit(ChillPath, CHILL_PATHFIND_MAX_PATH, pathLength, targetX, targetY);

        case pathfindmode_SecretExit:
            return ChillPathfinder::FindSecretExit(ChillPath, CHILL_PATHFIND_MAX_PATH, pathLength, targetX, targetY);

        case pathfindmode_HundredPercent:
            return ChillPathfinder::FindHundredPercentPath(ChillPath, CHILL_PATHFIND_MAX_PATH, pathLength, targetX, targetY);

        default:
            break;
    }

    return false;
}

static void Chill_DrawCurrentPath(byte *vidbuf, unsigned pitch)
{
    int pathLength = 0;
    int targetX = -1;
    int targetY = -1;

    if(!Chill_FindPathForCurrentMode(&pathLength, &targetX, &targetY))
    {
        return;
    }

    if(pathLength < 2)
    {
        return;
    }

    int drawPathLength = Chill_BuildWorldPath(pathLength);

    if(drawPathLength < 2)
    {
        return;
    }

    Chill_DrawFloorRasterPath(vidbuf, pitch, drawPathLength);

    (void)targetX;
    (void)targetY;
}

void Chill_HookWorld(byte *vidbuf, unsigned pitch)
{
    if(!Chill_IsInGameLevel() || vidbuf == NULL)
    {
        return;
    }

    Chill_EnsureColorTables();

    switch(PathfindMode)
    {
        case pathfindmode_ClosestPushableTile:
        case pathfindmode_AnyPercent:
        case pathfindmode_SecretExit:
        case pathfindmode_HundredPercent:
            Chill_DrawCurrentPath(vidbuf, pitch);
            break;

        default:
            break;
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

    int alpha = ((CHILL_MESSAGE_TICS - (int)elapsed) * CHILL_TEXT_ALPHA) / CHILL_MESSAGE_TICS;

    Chill_DrawFadeText(ChillMessageText, CHILL_TEXT_X, CHILL_TEXT_Y, alpha);
}
