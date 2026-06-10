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
#define CHILL_PATHFIND_MAX_OBJECTIVES (MAPSIZE * MAPSIZE)

#define CHILL_SEARCH_CLOSEST_PUSHABLE 0
#define CHILL_SEARCH_STANDARD_EXIT    1
#define CHILL_SEARCH_SECRET_EXIT      2
#define CHILL_SEARCH_OBJECTIVE        3

#define CHILL_OBJECTIVE_ITEM          1
#define CHILL_OBJECTIVE_ENEMY         2
#define CHILL_OBJECTIVE_PUSHABLE      3

static const int ChillPathDx[4] = { 0, 1, 0, -1 };
static const int ChillPathDy[4] = { -1, 0, 1, 0 };

static byte ChillVisited[CHILL_PATHFIND_STATE_COUNT];
static short ChillParentX[CHILL_PATHFIND_STATE_COUNT];
static short ChillParentY[CHILL_PATHFIND_STATE_COUNT];
static short ChillParentKeys[CHILL_PATHFIND_STATE_COUNT];
static int ChillQueue[CHILL_PATHFIND_STATE_COUNT];
static byte ChillKeyMaskAt[MAPSIZE][MAPSIZE];
static byte ChillSimOpenPushable[MAPSIZE][MAPSIZE];
static ChillPathPoint ChillReversePath[CHILL_PATHFIND_MAX_PATH];

static boolean ChillPath_CanPushFromTile(int pushTileX, int pushTileY, int standTileX, int standTileY);

typedef struct
{
    int type;
    int tilex;
    int tiley;
    int pushx;
    int pushy;
    boolean complete;
} ChillObjective;

static ChillObjective ChillObjectives[CHILL_PATHFIND_MAX_OBJECTIVES];
static int ChillObjectiveCount;

static boolean ChillPath_InBounds(int tilex, int tiley)
{
    return tilex >= 0 && tiley >= 0 && tilex < MAPSIZE && tiley < MAPSIZE;
}

static int ChillPath_StateIndex(int tilex, int tiley, int keys)
{
    return ((keys & (CHILL_PATHFIND_KEY_STATES - 1)) * MAPSIZE * MAPSIZE) + (tilex * MAPSIZE) + tiley;
}

static void ChillPath_DecodeState(int stateIndex, int *tilex, int *tiley, int *keys)
{
    int state = stateIndex;

    *keys = state / (MAPSIZE * MAPSIZE);
    state %= (MAPSIZE * MAPSIZE);
    *tilex = state / MAPSIZE;
    *tiley = state % MAPSIZE;
}

static int ChillPath_KeyMaskForItem(int itemNumber)
{
    if(itemNumber >= bo_key1 && itemNumber <= bo_key4)
    {
        return 1 << (itemNumber - bo_key1);
    }

    return 0;
}

static int ChillPath_KeyMaskDroppedByActor(objtype *ob)
{
    if(ob == NULL || !(ob->flags & FL_SHOOTABLE))
    {
        return 0;
    }

    switch(ob->obclass)
    {
        case bossobj:
        case gretelobj:
        case transobj:
        case uberobj:
        case willobj:
        case deathobj:
            return 1 << (bo_key1 - bo_key1);

        default:
            break;
    }

    return 0;
}

static boolean ChillPath_IsCollectableItemNumber(int itemNumber)
{
    switch(itemNumber)
    {
        case bo_alpo:
        case bo_firstaid:
        case bo_key1:
        case bo_key2:
        case bo_key3:
        case bo_key4:
        case bo_cross:
        case bo_chalice:
        case bo_bible:
        case bo_crown:
        case bo_clip:
        case bo_clip2:
        case bo_machinegun:
        case bo_chaingun:
        case bo_food:
        case bo_fullheal:
        case bo_25clip:
        case bo_spear:
            return true;

        default:
            break;
    }

    return false;
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

    // Some levels gate the exit/elevator behind a key dropped by a boss-class
    // enemy instead of a static key sprite. The real game creates the key at
    // the actor's tile when that actor dies, so the pathfinder can model this
    // as a collectible key on the actor's current tile.
    for(objtype *ob = (objtype *)player->next; ob != NULL; ob = (objtype *)ob->next)
    {
        int keyMask = ChillPath_KeyMaskDroppedByActor(ob);

        if(keyMask && ChillPath_InBounds(ob->tilex, ob->tiley))
        {
            ChillKeyMaskAt[ob->tilex][ob->tiley] |= (byte)keyMask;
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

    // Wolf3D doors are operable from both sides once the lock requirement is met.
    return true;
}


static int ChillPath_DoorIndexAt(int tilex, int tiley)
{
    if(!ChillPath_InBounds(tilex, tiley))
    {
        return -1;
    }

    byte tile = tilemap[tilex][tiley];

    if(!(tile & 0x80))
    {
        return -1;
    }

    int doorIndex = tile & 0x7f;

    if(doorIndex < 0 || doorIndex >= doornum)
    {
        return -1;
    }

    return doorIndex;
}

static boolean ChillPath_CanMoveThroughDoorEdge(int doorIndex, int fromTileX, int fromTileY, int toTileX, int toTileY, int keys)
{
    if(!ChillPath_CanPassDoor(doorIndex, keys))
    {
        return false;
    }

    doorobj_t *door = &doorobjlist[doorIndex];

    // Match the engine's physical door orientation. A vertical door is a
    // passage east/west; a horizontal door is a passage north/south. This
    // prevents the pathfinder from sneaking through the decorative side of an
    // elevator/locked door and getting the player stuck at the exit.
    if(door->vertical)
    {
        return fromTileY == door->tiley && toTileY == door->tiley;
    }

    return fromTileX == door->tilex && toTileX == door->tilex;
}

static boolean ChillPath_CanMoveBetweenTiles(int fromTileX, int fromTileY, int toTileX, int toTileY, int keys, int searchMode)
{
    if(!ChillPath_InBounds(fromTileX, fromTileY) || !ChillPath_InBounds(toTileX, toTileY))
    {
        return false;
    }

    if(ChillPathfinder::IsPushableTile(toTileX, toTileY))
    {
        if(searchMode == CHILL_SEARCH_CLOSEST_PUSHABLE)
        {
            return false;
        }

        return ChillPath_CanPushFromTile(toTileX, toTileY, fromTileX, fromTileY);
    }

    if(!ChillPathfinder::IsTileTraversableWithKeys(toTileX, toTileY, keys))
    {
        return false;
    }

    int fromDoor = ChillPath_DoorIndexAt(fromTileX, fromTileY);
    int toDoor = ChillPath_DoorIndexAt(toTileX, toTileY);

    if(fromDoor >= 0 && !ChillPath_CanMoveThroughDoorEdge(fromDoor, fromTileX, fromTileY, toTileX, toTileY, keys))
    {
        return false;
    }

    if(toDoor >= 0 && !ChillPath_CanMoveThroughDoorEdge(toDoor, fromTileX, fromTileY, toTileX, toTileY, keys))
    {
        return false;
    }

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
        return ChillSimOpenPushable[tilex][tiley] ? true : false;
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
    memset(ChillSimOpenPushable, 0, sizeof(ChillSimOpenPushable));
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

static boolean ChillPath_IsSecretElevatorStand(int tilex, int tiley)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[0] == NULL)
    {
        return false;
    }

    if(*(mapsegs[0] + (tiley << mapshift) + tilex) != ALTELEVATORTILE)
    {
        return false;
    }

    if(ChillPath_InBounds(tilex + 1, tiley) && tilemap[tilex + 1][tiley] == ELEVATORTILE)
    {
        return true;
    }

    if(ChillPath_InBounds(tilex - 1, tiley) && tilemap[tilex - 1][tiley] == ELEVATORTILE)
    {
        return true;
    }

    return false;
}

static boolean ChillPath_IsStandardElevatorStand(int tilex, int tiley)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[0] == NULL)
    {
        return false;
    }

    if(*(mapsegs[0] + (tiley << mapshift) + tilex) == ALTELEVATORTILE)
    {
        return false;
    }

    if(ChillPath_InBounds(tilex + 1, tiley) && tilemap[tilex + 1][tiley] == ELEVATORTILE)
    {
        return true;
    }

    if(ChillPath_InBounds(tilex - 1, tiley) && tilemap[tilex - 1][tiley] == ELEVATORTILE)
    {
        return true;
    }

    return false;
}

static boolean ChillPath_IsVictoryExitTile(int tilex, int tiley)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[1] == NULL)
    {
        return false;
    }

    return *(mapsegs[1] + (tiley << mapshift) + tilex) == EXITTILE;
}

static boolean ChillPath_IsStandardExitTile(int tilex, int tiley)
{
    if(ChillPath_IsVictoryExitTile(tilex, tiley))
    {
        return true;
    }

    return ChillPath_IsStandardElevatorStand(tilex, tiley);
}

static int ChillPath_FindObjectiveAt(int tilex, int tiley, int *targetTileX, int *targetTileY)
{
    for(int i = 0; i < ChillObjectiveCount; i++)
    {
        if(ChillObjectives[i].complete)
        {
            continue;
        }

        if(ChillObjectives[i].tilex == tilex && ChillObjectives[i].tiley == tiley)
        {
            if(targetTileX)
            {
                *targetTileX = ChillObjectives[i].pushx;
            }

            if(targetTileY)
            {
                *targetTileY = ChillObjectives[i].pushy;
            }

            return i;
        }
    }

    return -1;
}

static boolean ChillPath_SearchTarget
(
    int searchMode,
    int tilex,
    int tiley,
    int keys,
    int *targetTileX,
    int *targetTileY,
    int *objectiveIndex
)
{
    (void)keys;

    if(objectiveIndex)
    {
        *objectiveIndex = -1;
    }

    switch(searchMode)
    {
        case CHILL_SEARCH_CLOSEST_PUSHABLE:
            return ChillPath_IsAdjacentToPushableTile(tilex, tiley, targetTileX, targetTileY);

        case CHILL_SEARCH_STANDARD_EXIT:
            if(ChillPath_IsStandardExitTile(tilex, tiley))
            {
                if(targetTileX)
                {
                    *targetTileX = tilex;
                }

                if(targetTileY)
                {
                    *targetTileY = tiley;
                }

                return true;
            }
            break;

        case CHILL_SEARCH_SECRET_EXIT:
            if(ChillPath_IsSecretElevatorStand(tilex, tiley))
            {
                if(targetTileX)
                {
                    *targetTileX = tilex;
                }

                if(targetTileY)
                {
                    *targetTileY = tiley;
                }

                return true;
            }
            break;

        case CHILL_SEARCH_OBJECTIVE:
        {
            int objective = ChillPath_FindObjectiveAt(tilex, tiley, targetTileX, targetTileY);

            if(objective >= 0)
            {
                if(objectiveIndex)
                {
                    *objectiveIndex = objective;
                }

                return true;
            }
            break;
        }
    }

    return false;
}

static boolean ChillPath_CopySearchPath
(
    int foundIndex,
    ChillPathPoint *path,
    int maxPathLength,
    int *pathLength,
    int targetTileX,
    int targetTileY
)
{
    int reverseLength = 0;
    int walkIndex = foundIndex;

    while(walkIndex >= 0 && reverseLength < CHILL_PATHFIND_MAX_PATH)
    {
        int tilex;
        int tiley;
        int keys;

        ChillPath_DecodeState(walkIndex, &tilex, &tiley, &keys);

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

    if(targetTileX >= 0 && targetTileY >= 0 && outputLength > 0 && outputLength < maxPathLength)
    {
        if(path[outputLength - 1].tilex != targetTileX || path[outputLength - 1].tiley != targetTileY)
        {
            path[outputLength].tilex = targetTileX;
            path[outputLength].tiley = targetTileY;
            outputLength++;
        }
    }

    if(pathLength)
    {
        *pathLength = outputLength;
    }

    return outputLength > 1;
}

static boolean ChillPath_TrimPathToFirstNewKey
(
    ChillPathPoint *path,
    int *pathLength,
    int startKeys,
    int *targetTileX,
    int *targetTileY
)
{
    if(path == NULL || pathLength == NULL || *pathLength <= 1)
    {
        return false;
    }

    int keys = startKeys & (CHILL_PATHFIND_KEY_STATES - 1);

    for(int i = 0; i < *pathLength; i++)
    {
        int nextKeys = ChillPath_KeysAfterEnteringTile(path[i].tilex, path[i].tiley, keys);

        if((nextKeys & ~keys) != 0)
        {
            *pathLength = i + 1;

            if(targetTileX)
            {
                *targetTileX = path[i].tilex;
            }

            if(targetTileY)
            {
                *targetTileY = path[i].tiley;
            }

            return true;
        }

        keys = nextKeys;
    }

    return false;
}

static boolean ChillPath_RunSearch
(
    int startx,
    int starty,
    int startKeys,
    int searchMode,
    ChillPathPoint *path,
    int maxPathLength,
    int *pathLength,
    int *targetTileX,
    int *targetTileY,
    int *endTileX,
    int *endTileY,
    int *endKeys,
    int *objectiveIndex
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

    if(objectiveIndex)
    {
        *objectiveIndex = -1;
    }

    if(path == NULL || maxPathLength <= 0 || !ChillPath_InBounds(startx, starty))
    {
        return false;
    }

    ChillPath_BuildKeyPickupTable();
    memset(ChillVisited, 0, sizeof(ChillVisited));

    int initialKeys = ChillPath_KeysAfterEnteringTile(startx, starty, startKeys & (CHILL_PATHFIND_KEY_STATES - 1));
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
    int foundTargetTileX = -1;
    int foundTargetTileY = -1;
    int foundObjectiveIndex = -1;
    int foundKeys = initialKeys;

    while(head < tail)
    {
        int stateIndex = ChillQueue[head++];
        int keys;
        int tilex;
        int tiley;

        ChillPath_DecodeState(stateIndex, &tilex, &tiley, &keys);

        if(ChillPath_SearchTarget(searchMode, tilex, tiley, keys, &foundTargetTileX, &foundTargetTileY, &foundObjectiveIndex))
        {
            foundIndex = stateIndex;
            foundTileX = tilex;
            foundTileY = tiley;
            foundKeys = keys;
            break;
        }

        for(int i = 0; i < 4; i++)
        {
            int nextx = tilex + ChillPathDx[i];
            int nexty = tiley + ChillPathDy[i];

            if(!ChillPath_CanMoveBetweenTiles(tilex, tiley, nextx, nexty, keys, searchMode))
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

    if(targetTileX)
    {
        *targetTileX = foundTargetTileX;
    }

    if(targetTileY)
    {
        *targetTileY = foundTargetTileY;
    }

    if(endTileX)
    {
        *endTileX = foundTileX;
    }

    if(endTileY)
    {
        *endTileY = foundTileY;
    }

    if(endKeys)
    {
        *endKeys = foundKeys;
    }

    if(objectiveIndex)
    {
        *objectiveIndex = foundObjectiveIndex;
    }

    return ChillPath_CopySearchPath(foundIndex, path, maxPathLength, pathLength, foundTargetTileX, foundTargetTileY);
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
    if(player == NULL)
    {
        return false;
    }

    memset(ChillSimOpenPushable, 0, sizeof(ChillSimOpenPushable));

    return ChillPath_RunSearch
    (
        player->tilex,
        player->tiley,
        gamestate.keys,
        CHILL_SEARCH_CLOSEST_PUSHABLE,
        path,
        maxPathLength,
        pathLength,
        targetTileX,
        targetTileY,
        NULL,
        NULL,
        NULL,
        NULL
    );
}

boolean ChillPathfinder::FindStandardExit
(
    ChillPathPoint *path,
    int maxPathLength,
    int *pathLength,
    int *targetTileX,
    int *targetTileY
)
{
    if(player == NULL)
    {
        return false;
    }

    memset(ChillSimOpenPushable, 0, sizeof(ChillSimOpenPushable));

    boolean found = ChillPath_RunSearch
    (
        player->tilex,
        player->tiley,
        gamestate.keys,
        CHILL_SEARCH_STANDARD_EXIT,
        path,
        maxPathLength,
        pathLength,
        targetTileX,
        targetTileY,
        NULL,
        NULL,
        NULL,
        NULL
    );

    if(found)
    {
        ChillPath_TrimPathToFirstNewKey(path, pathLength, gamestate.keys, targetTileX, targetTileY);
    }

    return found;
}

boolean ChillPathfinder::FindSecretExit
(
    ChillPathPoint *path,
    int maxPathLength,
    int *pathLength,
    int *targetTileX,
    int *targetTileY
)
{
    if(player == NULL)
    {
        return false;
    }

    memset(ChillSimOpenPushable, 0, sizeof(ChillSimOpenPushable));

    boolean found = ChillPath_RunSearch
    (
        player->tilex,
        player->tiley,
        gamestate.keys,
        CHILL_SEARCH_SECRET_EXIT,
        path,
        maxPathLength,
        pathLength,
        targetTileX,
        targetTileY,
        NULL,
        NULL,
        NULL,
        NULL
    );

    if(found)
    {
        ChillPath_TrimPathToFirstNewKey(path, pathLength, gamestate.keys, targetTileX, targetTileY);
    }

    return found;
}

static boolean ChillPath_AddObjective(int type, int tilex, int tiley, int pushx, int pushy)
{
    if(ChillObjectiveCount >= CHILL_PATHFIND_MAX_OBJECTIVES)
    {
        return false;
    }

    if(!ChillPath_InBounds(tilex, tiley))
    {
        return false;
    }

    ChillObjectives[ChillObjectiveCount].type = type;
    ChillObjectives[ChillObjectiveCount].tilex = tilex;
    ChillObjectives[ChillObjectiveCount].tiley = tiley;
    ChillObjectives[ChillObjectiveCount].pushx = pushx;
    ChillObjectives[ChillObjectiveCount].pushy = pushy;
    ChillObjectives[ChillObjectiveCount].complete = false;
    ChillObjectiveCount++;

    return true;
}

static void ChillPath_BuildHundredPercentObjectives(void)
{
    ChillObjectiveCount = 0;

    for(objtype *ob = (objtype *)player->next; ob != NULL; ob = (objtype *)ob->next)
    {
        if(ob->state == NULL)
        {
            continue;
        }

        if(!(ob->flags & FL_SHOOTABLE))
        {
            continue;
        }

        if(!ChillPath_InBounds(ob->tilex, ob->tiley))
        {
            continue;
        }

        ChillPath_AddObjective(CHILL_OBJECTIVE_ENEMY, ob->tilex, ob->tiley, ob->tilex, ob->tiley);
    }

    for(statobj_t *stat = &statobjlist[0]; stat != laststatobj; stat++)
    {
        if(stat->shapenum == -1)
        {
            continue;
        }

        if(!(stat->flags & FL_BONUS))
        {
            continue;
        }

        if(!ChillPath_IsCollectableItemNumber(stat->itemnumber))
        {
            continue;
        }

        ChillPath_AddObjective(CHILL_OBJECTIVE_ITEM, stat->tilex, stat->tiley, stat->tilex, stat->tiley);
    }

    for(int y = 0; y < MAPSIZE; y++)
    {
        for(int x = 0; x < MAPSIZE; x++)
        {
            if(!ChillPathfinder::IsPushableTile(x, y))
            {
                continue;
            }

            for(int i = 0; i < 4; i++)
            {
                int standx = x - ChillPathDx[i];
                int standy = y - ChillPathDy[i];

                if(ChillPath_CanPushFromTile(x, y, standx, standy))
                {
                    ChillPath_AddObjective(CHILL_OBJECTIVE_PUSHABLE, standx, standy, x, y);
                }
            }
        }
    }
}

static int ChillPath_ObjectivesLeft(void)
{
    int left = 0;

    for(int i = 0; i < ChillObjectiveCount; i++)
    {
        if(!ChillObjectives[i].complete)
        {
            left++;
        }
    }

    return left;
}

static void ChillPath_CompletePushableObjective(int pushx, int pushy)
{
    ChillSimOpenPushable[pushx][pushy] = 1;

    for(int i = 0; i < ChillObjectiveCount; i++)
    {
        if(ChillObjectives[i].type == CHILL_OBJECTIVE_PUSHABLE && ChillObjectives[i].pushx == pushx && ChillObjectives[i].pushy == pushy)
        {
            ChillObjectives[i].complete = true;
        }
    }
}

static void ChillPath_MarkObjectivesOnTile(int tilex, int tiley)
{
    for(int i = 0; i < ChillObjectiveCount; i++)
    {
        if(ChillObjectives[i].complete)
        {
            continue;
        }

        if(ChillObjectives[i].tilex == tilex && ChillObjectives[i].tiley == tiley)
        {
            if(ChillObjectives[i].type == CHILL_OBJECTIVE_PUSHABLE)
            {
                ChillPath_CompletePushableObjective(ChillObjectives[i].pushx, ChillObjectives[i].pushy);
            }
            else
            {
                ChillObjectives[i].complete = true;
            }
        }
    }
}

static void ChillPath_MarkObjectivesOnPath(ChillPathPoint *path, int pathLength)
{
    for(int i = 0; i < pathLength; i++)
    {
        ChillPath_MarkObjectivesOnTile(path[i].tilex, path[i].tiley);
    }
}

static void ChillPath_AppendPath
(
    ChillPathPoint *destination,
    int maxDestinationLength,
    int *destinationLength,
    ChillPathPoint *source,
    int sourceLength
)
{
    int start = 0;

    if(*destinationLength > 0 && sourceLength > 0)
    {
        if(destination[*destinationLength - 1].tilex == source[0].tilex && destination[*destinationLength - 1].tiley == source[0].tiley)
        {
            start = 1;
        }
    }

    for(int i = start; i < sourceLength && *destinationLength < maxDestinationLength; i++)
    {
        destination[*destinationLength] = source[i];
        (*destinationLength)++;
    }
}

boolean ChillPathfinder::FindHundredPercentPath
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

    ChillPath_BuildHundredPercentObjectives();
    memset(ChillSimOpenPushable, 0, sizeof(ChillSimOpenPushable));

    int currentKeys = gamestate.keys & (CHILL_PATHFIND_KEY_STATES - 1);

    // A loaded game can resume after some objectives were already collected,
    // killed, or pushed. Remove anything already under the player first, then
    // show only the next actionable leg instead of the whole future route.
    ChillPath_MarkObjectivesOnTile(player->tilex, player->tiley);

    if(ChillPath_ObjectivesLeft() > 0)
    {
        boolean foundObjective = ChillPath_RunSearch
        (
            player->tilex,
            player->tiley,
            currentKeys,
            CHILL_SEARCH_OBJECTIVE,
            path,
            maxPathLength,
            pathLength,
            targetTileX,
            targetTileY,
            NULL,
            NULL,
            NULL,
            NULL
        );

        if(foundObjective)
        {
            ChillPath_TrimPathToFirstNewKey(path, pathLength, currentKeys, targetTileX, targetTileY);
        }

        return foundObjective;
    }

    boolean foundExit = ChillPath_RunSearch
    (
        player->tilex,
        player->tiley,
        currentKeys,
        CHILL_SEARCH_SECRET_EXIT,
        path,
        maxPathLength,
        pathLength,
        targetTileX,
        targetTileY,
        NULL,
        NULL,
        NULL,
        NULL
    );

    if(!foundExit)
    {
        foundExit = ChillPath_RunSearch
        (
            player->tilex,
            player->tiley,
            currentKeys,
            CHILL_SEARCH_STANDARD_EXIT,
            path,
            maxPathLength,
            pathLength,
            targetTileX,
            targetTileY,
            NULL,
            NULL,
            NULL,
            NULL
        );
    }

    if(foundExit)
    {
        ChillPath_TrimPathToFirstNewKey(path, pathLength, currentKeys, targetTileX, targetTileY);
    }

    return foundExit;
}

