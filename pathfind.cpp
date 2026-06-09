//
// pathfind.cpp
// Chocolate Wolfenstein 3D
//
// Intelligent tile-aware pathfinding for Wolfenstein 3D maps.
//

#include "pathfind.h"

#include <string.h>

#define CHILL_PATHFIND_KEY_COUNT 4
#define CHILL_PATHFIND_KEY_STATES (1 << CHILL_PATHFIND_KEY_COUNT)
#define CHILL_PATHFIND_STATE_COUNT (MAPSIZE * MAPSIZE * CHILL_PATHFIND_KEY_STATES)

static const int ChillPathDx[4] = { 0, 1, 0, -1 };
static const int ChillPathDy[4] = { -1, 0, 1, 0 };

static byte ChillVisited[CHILL_PATHFIND_STATE_COUNT];
static short ChillParentX[CHILL_PATHFIND_STATE_COUNT];
static short ChillParentY[CHILL_PATHFIND_STATE_COUNT];
static short ChillParentKeys[CHILL_PATHFIND_STATE_COUNT];
static int ChillQueue[CHILL_PATHFIND_STATE_COUNT];
static byte ChillKeyMaskAt[MAPSIZE][MAPSIZE];
static ChillPathPoint ChillReversePath[CHILL_PATHFIND_MAX_PATH];

static boolean ChillPath_InBounds(int tilex, int tiley)
{
    return tilex >= 0 && tiley >= 0 && tilex < MAPSIZE && tiley < MAPSIZE;
}

static int ChillPath_StateIndex(int tilex, int tiley, int keys)
{
    return ((keys & (CHILL_PATHFIND_KEY_STATES - 1)) * MAPSIZE * MAPSIZE) + (tilex * MAPSIZE) + tiley;
}

static int ChillPath_KeyMaskForItem(int itemNumber)
{
    if(itemNumber >= bo_key1 && itemNumber <= bo_key4)
    {
        return 1 << (itemNumber - bo_key1);
    }

    return 0;
}

static void ChillPath_BuildKeyPickupTable(void)
{
    memset(ChillKeyMaskAt, 0, sizeof(ChillKeyMaskAt));

    for(statobj_t *stat = &statobjlist[0]; stat != laststatobj; stat++)
    {
        if(stat->shapenum == -1)
        {
            continue;
        }

        int keyMask = ChillPath_KeyMaskForItem(stat->itemnumber);

        if(keyMask && ChillPath_InBounds(stat->tilex, stat->tiley))
        {
            ChillKeyMaskAt[stat->tilex][stat->tiley] |= (byte)keyMask;
        }
    }
}

static int ChillPath_KeysAfterEnteringTile(int tilex, int tiley, int keys)
{
    if(!ChillPath_InBounds(tilex, tiley))
    {
        return keys;
    }

    return (keys | ChillKeyMaskAt[tilex][tiley]) & (CHILL_PATHFIND_KEY_STATES - 1);
}

boolean ChillPathfinder::IsPushableTile(int tilex, int tiley)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[1] == NULL)
    {
        return false;
    }

    return *(mapsegs[1] + (tiley << mapshift) + tilex) == PUSHABLETILE;
}

static boolean ChillPath_IsActivePushwallTile(int tilex, int tiley)
{
    if(!pwallstate)
    {
        return false;
    }

    if(tilex == pwallx && tiley == pwally)
    {
        return true;
    }

    int dx = 0;
    int dy = 0;

    switch(pwalldir)
    {
        case di_north:
            dy = -1;
            break;

        case di_east:
            dx = 1;
            break;

        case di_south:
            dy = 1;
            break;

        case di_west:
            dx = -1;
            break;
    }

    return tilex == (int)pwallx + dx && tiley == (int)pwally + dy;
}

static boolean ChillPath_CanPassDoor(int doorIndex, int keys)
{
    if(doorIndex < 0 || doorIndex >= doornum)
    {
        return false;
    }

    doorobj_t *door = &doorobjlist[doorIndex];

    if(door->action == dr_open || door->action == dr_opening || doorposition[doorIndex] == 0xffff)
    {
        return true;
    }

    if(door->lock >= dr_lock1 && door->lock <= dr_lock4)
    {
        int requiredKey = 1 << (door->lock - dr_lock1);

        if(!(keys & requiredKey))
        {
            return false;
        }
    }

    // Wolfenstein's door model does not store one-way locks. If the required
    // key is available, the door can be operated from either side.
    return true;
}

static boolean ChillPath_BlockingStaticAt(int tilex, int tiley)
{
    objtype *actor = actorat[tilex][tiley];

    if(actor == NULL)
    {
        return false;
    }

    if(ISPOINTER(actor))
    {
        // Dynamic actors move; do not let a guard temporarily erase the route.
        return false;
    }

    if(ChillPathfinder::IsPushableTile(tilex, tiley) || ChillPath_IsActivePushwallTile(tilex, tiley))
    {
        return false;
    }

    return true;
}

boolean ChillPathfinder::IsTileTraversableWithKeys(int tilex, int tiley, int keys)
{
    if(!ChillPath_InBounds(tilex, tiley))
    {
        return false;
    }

    if(ChillPathfinder::IsPushableTile(tilex, tiley))
    {
        return true;
    }

    byte tile = tilemap[tilex][tiley];

    if(tile & 0x80)
    {
        return ChillPath_CanPassDoor(tile & 0x7f, keys);
    }

    if(ChillPath_IsActivePushwallTile(tilex, tiley))
    {
        return true;
    }

    if(ChillPath_BlockingStaticAt(tilex, tiley))
    {
        return false;
    }

    byte baseTile = tile & ~0x40;

    if(baseTile == 0)
    {
        return true;
    }

    if(baseTile >= AREATILE)
    {
        return true;
    }

    return false;
}

boolean ChillPathfinder::IsTileTraversable(int tilex, int tiley)
{
    return ChillPathfinder::IsTileTraversableWithKeys(tilex, tiley, gamestate.keys & (CHILL_PATHFIND_KEY_STATES - 1));
}

static int ChillPath_DirectionFromTileToTile(int fromTileX, int fromTileY, int toTileX, int toTileY)
{
    for(int i = 0; i < 4; i++)
    {
        if(fromTileX + ChillPathDx[i] == toTileX && fromTileY + ChillPathDy[i] == toTileY)
        {
            return i;
        }
    }

    return -1;
}

static boolean ChillPath_CanPushFromTile(int pushTileX, int pushTileY, int standTileX, int standTileY)
{
    if(!ChillPathfinder::IsPushableTile(pushTileX, pushTileY))
    {
        return false;
    }

    if(!ChillPath_InBounds(standTileX, standTileY))
    {
        return false;
    }

    // Cmd_Use() pushes in the direction from the player tile into the
    // pushwall tile. PushWall() then requires the tile behind the pushwall
    // in that same direction to be clear. Mirror that rule here so the
    // pathfinder only targets a side that can actually activate the secret.
    int pushDirection = ChillPath_DirectionFromTileToTile(standTileX, standTileY, pushTileX, pushTileY);

    if(pushDirection < 0)
    {
        return false;
    }

    int behindTileX = pushTileX + ChillPathDx[pushDirection];
    int behindTileY = pushTileY + ChillPathDy[pushDirection];

    if(!ChillPath_InBounds(behindTileX, behindTileY))
    {
        return false;
    }

    if(tilemap[pushTileX][pushTileY] == 0)
    {
        return false;
    }

    if(actorat[behindTileX][behindTileY])
    {
        return false;
    }

    return true;
}

static boolean ChillPath_IsAdjacentToPushableTile(int tilex, int tiley, int *targetTileX, int *targetTileY)
{
    for(int i = 0; i < 4; i++)
    {
        int checkx = tilex + ChillPathDx[i];
        int checky = tiley + ChillPathDy[i];

        if(ChillPath_CanPushFromTile(checkx, checky, tilex, tiley))
        {
            if(targetTileX)
            {
                *targetTileX = checkx;
            }

            if(targetTileY)
            {
                *targetTileY = checky;
            }

            return true;
        }
    }

    return false;
}

boolean ChillPathfinder::FindClosestPushableTile
(
    ChillPathPoint *path,
    int maxPathLength,
    int *pathLength,
    int *targetTileX,
    int *targetTileY
)
{
    if(pathLength)
    {
        *pathLength = 0;
    }

    if(targetTileX)
    {
        *targetTileX = -1;
    }

    if(targetTileY)
    {
        *targetTileY = -1;
    }

    if(player == NULL || path == NULL || maxPathLength <= 0)
    {
        return false;
    }

    int startx = player->tilex;
    int starty = player->tiley;

    if(!ChillPath_InBounds(startx, starty))
    {
        return false;
    }

    ChillPath_BuildKeyPickupTable();
    memset(ChillVisited, 0, sizeof(ChillVisited));

    int initialKeys = ChillPath_KeysAfterEnteringTile(startx, starty, gamestate.keys & (CHILL_PATHFIND_KEY_STATES - 1));
    int startIndex = ChillPath_StateIndex(startx, starty, initialKeys);

    ChillVisited[startIndex] = 1;
    ChillParentX[startIndex] = -1;
    ChillParentY[startIndex] = -1;
    ChillParentKeys[startIndex] = -1;

    int head = 0;
    int tail = 0;

    ChillQueue[tail++] = startIndex;

    int foundIndex = -1;
    int foundTileX = -1;
    int foundTileY = -1;

    while(head < tail)
    {
        int stateIndex = ChillQueue[head++];
        int state = stateIndex;
        int keys = state / (MAPSIZE * MAPSIZE);
        state %= (MAPSIZE * MAPSIZE);
        int tilex = state / MAPSIZE;
        int tiley = state % MAPSIZE;

        if(ChillPath_IsAdjacentToPushableTile(tilex, tiley, &foundTileX, &foundTileY))
        {
            foundIndex = stateIndex;
            break;
        }

        for(int i = 0; i < 4; i++)
        {
            int nextx = tilex + ChillPathDx[i];
            int nexty = tiley + ChillPathDy[i];

            // For this mode, the destination is the reachable tile beside a
            // pushwall. Do not walk through an unopened pushwall while looking
            // for that reachable side, even though generic traversability keeps
            // pushable tiles passable for future pathfind modes.
            if(ChillPathfinder::IsPushableTile(nextx, nexty))
            {
                continue;
            }

            if(!ChillPathfinder::IsTileTraversableWithKeys(nextx, nexty, keys))
            {
                continue;
            }

            int nextKeys = ChillPath_KeysAfterEnteringTile(nextx, nexty, keys);
            int nextIndex = ChillPath_StateIndex(nextx, nexty, nextKeys);

            if(ChillVisited[nextIndex])
            {
                continue;
            }

            ChillVisited[nextIndex] = 1;
            ChillParentX[nextIndex] = (short)tilex;
            ChillParentY[nextIndex] = (short)tiley;
            ChillParentKeys[nextIndex] = (short)keys;

            if(tail < CHILL_PATHFIND_STATE_COUNT)
            {
                ChillQueue[tail++] = nextIndex;
            }
        }
    }

    if(foundIndex < 0)
    {
        return false;
    }

    int reverseLength = 0;
    int walkIndex = foundIndex;

    while(walkIndex >= 0 && reverseLength < CHILL_PATHFIND_MAX_PATH)
    {
        int state = walkIndex;
        int keys = state / (MAPSIZE * MAPSIZE);
        state %= (MAPSIZE * MAPSIZE);
        int tilex = state / MAPSIZE;
        int tiley = state % MAPSIZE;

        ChillReversePath[reverseLength].tilex = tilex;
        ChillReversePath[reverseLength].tiley = tiley;
        reverseLength++;

        short parentX = ChillParentX[walkIndex];
        short parentY = ChillParentY[walkIndex];
        short parentKeys = ChillParentKeys[walkIndex];

        if(parentX < 0 || parentY < 0 || parentKeys < 0)
        {
            break;
        }

        (void)keys;
        walkIndex = ChillPath_StateIndex(parentX, parentY, parentKeys);
    }

    int outputLength = 0;

    for(int i = reverseLength - 1; i >= 0 && outputLength < maxPathLength; i--)
    {
        path[outputLength++] = ChillReversePath[i];
    }

    if(outputLength < maxPathLength)
    {
        path[outputLength].tilex = foundTileX;
        path[outputLength].tiley = foundTileY;
        outputLength++;
    }

    if(pathLength)
    {
        *pathLength = outputLength;
    }

    if(targetTileX)
    {
        *targetTileX = foundTileX;
    }

    if(targetTileY)
    {
        *targetTileY = foundTileY;
    }

    return outputLength > 1;
}
