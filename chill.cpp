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
boolean ChillPathDebugEnabled = false;

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
    "100%",
    "Closest ammo",
    "Closest ammo with enemies",
    "Closest health"
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

static void Chill_WriteDebugDump(void);

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

static const char *Chill_GetDebugToggleString(void)
{
    return ChillPathDebugEnabled ? "On" : "Off";
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

    if(key == CHILL_PATH_DEBUG_KEY)
    {
        ChillPathDebugEnabled = !ChillPathDebugEnabled;
        Chill_WriteDebugDump();
        Chill_ShowModeMessage("Path debug", Chill_GetDebugToggleString());
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

    // Keep every grid step instead of reducing the path to only corner points.
    // The renderer draws world-space line segments between these points; when
    // the path was simplified, a long straight visual segment could cut across
    // a solid wall even though the grid route itself walked around it. Using
    // every tile center makes the drawn trail follow the exact BFS route.
    int visiblePathLength = pathLength;
    int maxVisiblePoints = CHILL_PATH_VISIBLE_TILE_LIMIT + 1;

    if(maxVisiblePoints < 2)
    {
        maxVisiblePoints = 2;
    }

    if(visiblePathLength > maxVisiblePoints)
    {
        visiblePathLength = maxVisiblePoints;
    }

    for(int i = 0; i < visiblePathLength && drawLength < CHILL_PATH_MAX_DRAW_POINTS; i++)
    {
        ChillPathWorldX[drawLength] = ((fixed)ChillPath[i].tilex << TILESHIFT) + (TILEGLOBAL >> 1);
        ChillPathWorldY[drawLength] = ((fixed)ChillPath[i].tiley << TILESHIFT) + (TILEGLOBAL >> 1);
        ChillPathCumulativeDistance[drawLength] = 0;
        drawLength++;
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

    if(!spotvis[tilex][tiley])
    {
        return false;
    }

    // spotvis is also set for visible wall-hit tiles. The path trail is a
    // floor overlay, so do not paint path pixels inside solid wall tiles just
    // because the raycaster saw that wall. This fixes paths/arrows appearing
    // to cut directly through a wall on maps with visible wall cells along the
    // route projection.
    if(!ChillPathfinder::IsTileTraversableWithKeys(tilex, tiley, gamestate.keys))
    {
        return false;
    }

    return true;
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

        case pathfindmode_ClosestAmmo:
            return ChillPathfinder::FindClosestAmmo(ChillPath, CHILL_PATHFIND_MAX_PATH, pathLength, targetX, targetY);

        case pathfindmode_ClosestAmmoWithEnemies:
            return ChillPathfinder::FindClosestAmmoWithEnemies(ChillPath, CHILL_PATHFIND_MAX_PATH, pathLength, targetX, targetY);

        case pathfindmode_ClosestHealth:
            return ChillPathfinder::FindClosestHealth(ChillPath, CHILL_PATHFIND_MAX_PATH, pathLength, targetX, targetY);

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
        case pathfindmode_ClosestAmmo:
        case pathfindmode_ClosestAmmoWithEnemies:
        case pathfindmode_ClosestHealth:
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


static const char *Chill_GetGoalDescription(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE)
    {
        return "None";
    }

    if(ChillPathfinder::IsPushableTile(tilex, tiley))
    {
        return "Push wall";
    }

    if(ChillPathfinder::IsVerifiedSecretExitTile(tilex, tiley))
    {
        return "Secret elevator";
    }

    if(ChillPathfinder::IsVerifiedStandardExitTile(tilex, tiley))
    {
        if(ChillPathfinder::IsVerifiedVictoryExitTile(tilex, tiley))
        {
            return "Victory exit";
        }

        return "Standard elevator";
    }

    if(tilemap[tilex][tiley] == ELEVATORTILE)
    {
        return "Elevator wall";
    }

    if(tilemap[tilex][tiley] & 0x80)
    {
        return "Door";
    }

    for(statobj_t *stat = &statobjlist[0]; stat != laststatobj; stat++)
    {
        if(stat->shapenum == -1 || !(stat->flags & FL_BONUS))
        {
            continue;
        }

        if(stat->tilex != tilex || stat->tiley != tiley)
        {
            continue;
        }

        switch(stat->itemnumber)
        {
            case bo_key1:
                return "Gold key";

            case bo_key2:
                return "Silver key";

            case bo_key3:
                return "Key 3";

            case bo_key4:
                return "Key 4";

            case bo_clip:
            case bo_clip2:
            case bo_25clip:
                return "Ammo";

            case bo_machinegun:
            case bo_chaingun:
                return "Gun";

            case bo_alpo:
            case bo_firstaid:
            case bo_food:
            case bo_fullheal:
            case bo_gibs:
                return "Health";

            case bo_cross:
            case bo_chalice:
            case bo_bible:
            case bo_crown:
                return "Treasure";

            default:
                return "Bonus";
        }
    }

    for(objtype *ob = (objtype *)player->next; ob != NULL; ob = (objtype *)ob->next)
    {
        if(!(ob->flags & FL_SHOOTABLE))
        {
            continue;
        }

        if(ob->tilex == tilex && ob->tiley == tiley)
        {
            return "Enemy";
        }
    }

    return "Tile";
}


static void Chill_JsonString(FILE *file, const char *text)
{
    fputc('"', file);

    if(text != NULL)
    {
        while(*text)
        {
            unsigned char ch = (unsigned char)*text++;

            switch(ch)
            {
                case '"':
                    fputs("\\\"", file);
                    break;

                case '\\':
                    fputs("\\\\", file);
                    break;

                case '\b':
                    fputs("\\b", file);
                    break;

                case '\f':
                    fputs("\\f", file);
                    break;

                case '\n':
                    fputs("\\n", file);
                    break;

                case '\r':
                    fputs("\\r", file);
                    break;

                case '\t':
                    fputs("\\t", file);
                    break;

                default:
                    if(ch < 32)
                    {
                        fprintf(file, "\\u%04x", ch);
                    }
                    else
                    {
                        fputc(ch, file);
                    }
                    break;
            }
        }
    }

    fputc('"', file);
}

static int Chill_Map0At(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE || mapsegs[0] == NULL)
    {
        return -1;
    }

    return *(mapsegs[0] + (tiley << mapshift) + tilex);
}

static int Chill_Map1At(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE || mapsegs[1] == NULL)
    {
        return -1;
    }

    return *(mapsegs[1] + (tiley << mapshift) + tilex);
}

static int Chill_TileMapAt(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE)
    {
        return -1;
    }

    return tilemap[tilex][tiley];
}

static int Chill_ActorKindAt(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE)
    {
        return -1;
    }

    objtype *actor = actorat[tilex][tiley];

    if(actor == NULL)
    {
        return 0;
    }

    if(ISPOINTER(actor))
    {
        return 1;
    }

    return 2;
}

static int Chill_ActorClassAt(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE)
    {
        return -1;
    }

    objtype *actor = actorat[tilex][tiley];

    if(actor != NULL && ISPOINTER(actor))
    {
        return actor->obclass;
    }

    return -1;
}

static int Chill_ActorRawAt(int tilex, int tiley)
{
    if(tilex < 0 || tiley < 0 || tilex >= MAPSIZE || tiley >= MAPSIZE)
    {
        return 0;
    }

    objtype *actor = actorat[tilex][tiley];

    if(actor == NULL || ISPOINTER(actor))
    {
        return 0;
    }

    return (int)(uintptr_t)actor;
}

static void Chill_DumpTileJson(FILE *file, int tilex, int tiley)
{
    fprintf
    (
        file,
        "{\"x\":%d,\"y\":%d,\"m0\":%d,\"m1\":%d,\"t\":%d,\"actorKind\":%d,\"actorClass\":%d,\"actorRaw\":%d,\"trav\":%d,\"push\":%d,\"stdExit\":%d,\"secExit\":%d,\"victoryExit\":%d}",
        tilex,
        tiley,
        Chill_Map0At(tilex, tiley),
        Chill_Map1At(tilex, tiley),
        Chill_TileMapAt(tilex, tiley),
        Chill_ActorKindAt(tilex, tiley),
        Chill_ActorClassAt(tilex, tiley),
        Chill_ActorRawAt(tilex, tiley),
        ChillPathfinder::IsTileTraversable(tilex, tiley) ? 1 : 0,
        ChillPathfinder::IsPushableTile(tilex, tiley) ? 1 : 0,
        ChillPathfinder::IsVerifiedStandardExitTile(tilex, tiley) ? 1 : 0,
        ChillPathfinder::IsVerifiedSecretExitTile(tilex, tiley) ? 1 : 0,
        ChillPathfinder::IsVerifiedVictoryExitTile(tilex, tiley) ? 1 : 0
    );
}

static void Chill_DumpTileWindowJson(FILE *file, const char *name, int centerX, int centerY, int radius)
{
    fprintf(file, ",\"");
    fputs(name, file);
    fprintf(file, "\":[");

    boolean first = true;

    for(int y = centerY - radius; y <= centerY + radius; y++)
    {
        for(int x = centerX - radius; x <= centerX + radius; x++)
        {
            if(x < 0 || y < 0 || x >= MAPSIZE || y >= MAPSIZE)
            {
                continue;
            }

            if(!first)
            {
                fputc(',', file);
            }

            first = false;
            Chill_DumpTileJson(file, x, y);
        }
    }

    fputc(']', file);
}

static void Chill_DumpAllPushablesJson(FILE *file)
{
    fputs(",\"pushables\":[", file);

    boolean first = true;

    for(int y = 0; y < MAPSIZE; y++)
    {
        for(int x = 0; x < MAPSIZE; x++)
        {
            if(Chill_Map1At(x, y) != PUSHABLETILE)
            {
                continue;
            }

            if(!first)
            {
                fputc(',', file);
            }

            first = false;
            Chill_DumpTileJson(file, x, y);
        }
    }

    fputc(']', file);
}

static void Chill_DumpRawExitMarkersJson(FILE *file)
{
    fputs(",\"rawExitMarkers\":[", file);

    boolean first = true;

    for(int y = 0; y < MAPSIZE; y++)
    {
        for(int x = 0; x < MAPSIZE; x++)
        {
            int m0 = Chill_Map0At(x, y);
            int m1 = Chill_Map1At(x, y);
            int t = Chill_TileMapAt(x, y);

            if(m0 != ELEVATORTILE && t != ELEVATORTILE && m0 != ALTELEVATORTILE && m1 != EXITTILE)
            {
                continue;
            }

            if(!first)
            {
                fputc(',', file);
            }

            first = false;
            Chill_DumpTileJson(file, x, y);
        }
    }

    fputc(']', file);
}

static void Chill_DumpDoorsJson(FILE *file)
{
    fputs(",\"doors\":[", file);

    for(int i = 0; i < doornum; i++)
    {
        doorobj_t *door = &doorobjlist[i];

        if(i > 0)
        {
            fputc(',', file);
        }

        fprintf
        (
            file,
            "{\"i\":%d,\"x\":%d,\"y\":%d,\"vertical\":%d,\"lock\":%d,\"action\":%d,\"tic\":%d,\"position\":%u}",
            i,
            door->tilex,
            door->tiley,
            door->vertical ? 1 : 0,
            door->lock,
            door->action,
            door->ticcount,
            doorposition[i]
        );
    }

    fputc(']', file);
}

static void Chill_DumpStaticsJson(FILE *file)
{
    fputs(",\"statics\":[", file);

    boolean first = true;

    for(statobj_t *stat = &statobjlist[0]; stat != laststatobj; stat++)
    {
        if(stat->shapenum == -1)
        {
            continue;
        }

        if(!first)
        {
            fputc(',', file);
        }

        first = false;
        fprintf
        (
            file,
            "{\"x\":%d,\"y\":%d,\"shape\":%d,\"flags\":%u,\"item\":%d,\"bonus\":%d}",
            stat->tilex,
            stat->tiley,
            stat->shapenum,
            (unsigned)stat->flags,
            stat->itemnumber,
            (stat->flags & FL_BONUS) ? 1 : 0
        );
    }

    fputc(']', file);
}

static void Chill_DumpActorsJson(FILE *file)
{
    fputs(",\"actors\":[", file);

    boolean first = true;

    for(objtype *ob = (player != NULL) ? (objtype *)player->next : NULL; ob != NULL; ob = (objtype *)ob->next)
    {
        if(!first)
        {
            fputc(',', file);
        }

        first = false;
        fprintf
        (
            file,
            "{\"x\":%d,\"y\":%d,\"class\":%d,\"flags\":%u,\"hp\":%d,\"active\":%d,\"hidden\":%d,\"shootable\":%d}",
            ob->tilex,
            ob->tiley,
            ob->obclass,
            (unsigned)ob->flags,
            ob->hitpoints,
            ob->active,
            ob->hidden,
            (ob->flags & FL_SHOOTABLE) ? 1 : 0
        );
    }

    fputc(']', file);
}

static void Chill_DumpPathJson(FILE *file, int pathLength)
{
    fputs(",\"path\":[", file);

    for(int i = 0; i < pathLength; i++)
    {
        if(i > 0)
        {
            fputc(',', file);
        }

        fprintf(file, "[%d,%d]", ChillPath[i].tilex, ChillPath[i].tiley);
    }

    fputc(']', file);
}


static void Chill_DumpFullMapIntArray(FILE *file, const char *name, int kind)
{
    fprintf(file, ",\"%s\":[", name);

    boolean first = true;

    for(int y = 0; y < MAPSIZE; y++)
    {
        for(int x = 0; x < MAPSIZE; x++)
        {
            if(!first)
            {
                fputc(',', file);
            }

            first = false;

            switch(kind)
            {
                case 0:
                    fprintf(file, "%d", Chill_Map0At(x, y));
                    break;

                case 1:
                    fprintf(file, "%d", Chill_Map1At(x, y));
                    break;

                case 2:
                    fprintf(file, "%d", Chill_TileMapAt(x, y));
                    break;

                case 3:
                    fprintf(file, "%d", Chill_ActorKindAt(x, y));
                    break;

                case 4:
                    fprintf(file, "%d", Chill_ActorClassAt(x, y));
                    break;

                case 5:
                    fprintf(file, "%d", Chill_ActorRawAt(x, y));
                    break;

                case 6:
                    fprintf(file, "%d", ChillPathfinder::IsTileTraversable(x, y) ? 1 : 0);
                    break;

                case 7:
                    fprintf(file, "%d", ChillPathfinder::IsPushableTile(x, y) ? 1 : 0);
                    break;

                case 8:
                    fprintf(file, "%d", ChillPathfinder::IsVerifiedStandardExitTile(x, y) ? 1 : 0);
                    break;

                case 9:
                    fprintf(file, "%d", ChillPathfinder::IsVerifiedSecretExitTile(x, y) ? 1 : 0);
                    break;

                case 10:
                    fprintf(file, "%d", ChillPathfinder::IsVerifiedVictoryExitTile(x, y) ? 1 : 0);
                    break;

                case 11:
                    fprintf(file, "%d", spotvis[x][y]);
                    break;

                default:
                    fputc('0', file);
                    break;
            }
        }
    }

    fputc(']', file);
}

static void Chill_DumpFullMapJson(FILE *file)
{
    fprintf(file, ",\"fullMap\":{\"w\":%d,\"h\":%d,\"mapshift\":%d", MAPSIZE, MAPSIZE, mapshift);

    Chill_DumpFullMapIntArray(file, "plane0", 0);
    Chill_DumpFullMapIntArray(file, "plane1", 1);
    Chill_DumpFullMapIntArray(file, "tilemap", 2);
    Chill_DumpFullMapIntArray(file, "actorKind", 3);
    Chill_DumpFullMapIntArray(file, "actorClass", 4);
    Chill_DumpFullMapIntArray(file, "actorRaw", 5);
    Chill_DumpFullMapIntArray(file, "traversable", 6);
    Chill_DumpFullMapIntArray(file, "pushable", 7);
    Chill_DumpFullMapIntArray(file, "standardExit", 8);
    Chill_DumpFullMapIntArray(file, "secretExit", 9);
    Chill_DumpFullMapIntArray(file, "victoryExit", 10);
    Chill_DumpFullMapIntArray(file, "spotvis", 11);

    fputc('}', file);
}


static void Chill_WriteDebugDump(void)
{
    FILE *file = fopen(CHILL_PATH_DEBUG_DUMP_FILE, "wb");

    if(file == NULL)
    {
        return;
    }

    int pathLength = 0;
    int targetX = -1;
    int targetY = -1;
    boolean hasPath = Chill_FindPathForCurrentMode(&pathLength, &targetX, &targetY);
    int pathEndX = -1;
    int pathEndY = -1;

    if(hasPath && pathLength > 0)
    {
        pathEndX = ChillPath[pathLength - 1].tilex;
        pathEndY = ChillPath[pathLength - 1].tiley;
    }

    fputs("{\"schema\":1", file);
    fputs(",\"build\":\"chill-path-debug\"", file);

    fprintf
    (
        file,
        ",\"modes\":{\"highlight\":%d,\"highlightName\":",
        HighlightMode
    );
    Chill_JsonString(file, HighlightModeStrings[HighlightMode]);
    fprintf(file, ",\"pathfind\":%d,\"pathfindName\":", PathfindMode);
    Chill_JsonString(file, PathfindModeStrings[PathfindMode]);
    fprintf(file, ",\"debug\":%d}", ChillPathDebugEnabled ? 1 : 0);

    fprintf
    (
        file,
        ",\"game\":{\"ingame\":%d,\"playstate\":%d,\"victory\":%d,\"episode\":%d,\"mapon\":%d,\"difficulty\":%d,\"keys\":%d,\"health\":%d,\"ammo\":%d,\"weapon\":%d,\"bestweapon\":%d,\"secret\":%d,\"secretTotal\":%d,\"treasure\":%d,\"treasureTotal\":%d,\"kills\":%d,\"killTotal\":%d,\"time\":%d}",
        ingame ? 1 : 0,
        playstate,
        gamestate.victoryflag ? 1 : 0,
        gamestate.episode,
        gamestate.mapon,
        gamestate.difficulty,
        gamestate.keys,
        gamestate.health,
        gamestate.ammo,
        gamestate.weapon,
        gamestate.bestweapon,
        gamestate.secretcount,
        gamestate.secrettotal,
        gamestate.treasurecount,
        gamestate.treasuretotal,
        gamestate.killcount,
        gamestate.killtotal,
        gamestate.TimeCount
    );

    if(player != NULL)
    {
        fprintf
        (
            file,
            ",\"player\":{\"tilex\":%d,\"tiley\":%d,\"x\":%d,\"y\":%d,\"angle\":%d,\"area\":%d}",
            player->tilex,
            player->tiley,
            player->x,
            player->y,
            player->angle,
            player->areanumber
        );
    }
    else
    {
        fputs(",\"player\":null", file);
    }

    fprintf
    (
        file,
        ",\"pushwall\":{\"state\":%u,\"pos\":%u,\"x\":%u,\"y\":%u,\"dir\":%u,\"tile\":%u}",
        pwallstate,
        pwallpos,
        pwallx,
        pwally,
        pwalldir,
        pwalltile
    );

    fprintf
    (
        file,
        ",\"currentRoute\":{\"hasPath\":%d,\"pathLength\":%d,\"targetX\":%d,\"targetY\":%d,\"targetType\":",
        hasPath ? 1 : 0,
        pathLength,
        targetX,
        targetY
    );
    Chill_JsonString(file, Chill_GetGoalDescription(targetX, targetY));
    fprintf(file, ",\"pathEndX\":%d,\"pathEndY\":%d}", pathEndX, pathEndY);

    if(player != NULL)
    {
        fputs(",\"playerTile\":", file);
        Chill_DumpTileJson(file, player->tilex, player->tiley);
        Chill_DumpTileWindowJson(file, "playerWindow", player->tilex, player->tiley, 6);
    }

    if(targetX >= 0 && targetY >= 0)
    {
        fputs(",\"targetTile\":", file);
        Chill_DumpTileJson(file, targetX, targetY);
        Chill_DumpTileWindowJson(file, "targetWindow", targetX, targetY, 6);
    }

    Chill_DumpPathJson(file, pathLength);
    Chill_DumpFullMapJson(file);
    Chill_DumpAllPushablesJson(file);
    Chill_DumpRawExitMarkersJson(file);
    Chill_DumpDoorsJson(file);
    Chill_DumpStaticsJson(file);
    Chill_DumpActorsJson(file);

    fputs("}\n", file);
    fclose(file);
}

static void Chill_DrawDebugOverlay(void)
{
    if(!ChillPathDebugEnabled)
    {
        return;
    }

    int pathLength = 0;
    int targetX = -1;
    int targetY = -1;
    boolean hasPath = Chill_FindPathForCurrentMode(&pathLength, &targetX, &targetY);
    int pathEndX = -1;
    int pathEndY = -1;

    if(hasPath && pathLength > 0)
    {
        pathEndX = ChillPath[pathLength - 1].tilex;
        pathEndY = ChillPath[pathLength - 1].tiley;
    }

    char line[96];
    int y = CHILL_DEBUG_TEXT_Y;

    snprintf(line, sizeof(line), "Debug: %s", Chill_GetDebugToggleString());
    Chill_DrawFadeText(line, CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
    y += CHILL_DEBUG_TEXT_LINE_HEIGHT;

    snprintf(line, sizeof(line), "Mode: %s", PathfindModeStrings[PathfindMode]);
    Chill_DrawFadeText(line, CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
    y += CHILL_DEBUG_TEXT_LINE_HEIGHT;

    snprintf(line, sizeof(line), "Player tile: %d,%d", player->tilex, player->tiley);
    Chill_DrawFadeText(line, CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
    y += CHILL_DEBUG_TEXT_LINE_HEIGHT;

    if(hasPath)
    {
        snprintf(line, sizeof(line), "Goal tile: %d,%d", targetX, targetY);
        Chill_DrawFadeText(line, CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
        y += CHILL_DEBUG_TEXT_LINE_HEIGHT;

        snprintf(line, sizeof(line), "Goal type: %s", Chill_GetGoalDescription(targetX, targetY));
        Chill_DrawFadeText(line, CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
        y += CHILL_DEBUG_TEXT_LINE_HEIGHT;

        snprintf(line, sizeof(line), "Path end: %d,%d len:%d", pathEndX, pathEndY, pathLength);
        Chill_DrawFadeText(line, CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
        y += CHILL_DEBUG_TEXT_LINE_HEIGHT;

        if(targetX >= 0 && targetY >= 0 && targetX < MAPSIZE && targetY < MAPSIZE && mapsegs[0] != NULL && mapsegs[1] != NULL)
        {
            int offset = (targetY << mapshift) + targetX;

            snprintf
            (
                line,
                sizeof(line),
                "Raw m0:%d m1:%d t:%d a:%d",
                *(mapsegs[0] + offset),
                *(mapsegs[1] + offset),
                tilemap[targetX][targetY],
                actorat[targetX][targetY] ? 1 : 0
            );

            Chill_DrawFadeText(line, CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
        }
    }
    else
    {
        Chill_DrawFadeText("Goal tile: none", CHILL_DEBUG_TEXT_X, y, CHILL_DEBUG_TEXT_ALPHA);
    }
}

void Chill_HookOverlay(void)
{
    if(!Chill_IsInGameLevel())
    {
        return;
    }

    Chill_DrawDebugOverlay();

    longword now = GetTimeCount();
    longword elapsed = now - ChillMessageStartTime;

    if(ChillMessageStartTime == 0 || elapsed >= CHILL_MESSAGE_TICS)
    {
        return;
    }

    int alpha = ((CHILL_MESSAGE_TICS - (int)elapsed) * CHILL_TEXT_ALPHA) / CHILL_MESSAGE_TICS;

    Chill_DrawFadeText(ChillMessageText, CHILL_TEXT_X, CHILL_TEXT_Y, alpha);
}
