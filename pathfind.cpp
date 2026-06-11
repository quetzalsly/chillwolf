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
#define CHILL_SEARCH_AMMO             4
#define CHILL_SEARCH_AMMO_WITH_ENEMIES 5
#define CHILL_SEARCH_HEALTH           6

#define CHILL_OBJECTIVE_TREASURE      1
#define CHILL_OBJECTIVE_ENEMY         2
#define CHILL_OBJECTIVE_KEY           3
#define CHILL_OBJECTIVE_PUSHABLE      4

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
static int ChillPath_DirectionFromTileToTile(int fromTileX, int fromTileY, int toTileX, int toTileY);

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
#ifndef SPEAR
        case bossobj:
        case gretelobj:
            return 1 << (bo_key1 - bo_key1);
#else
        case transobj:
        case uberobj:
        case willobj:
        case deathobj:
            return 1 << (bo_key1 - bo_key1);
#endif

        default:
            break;
    }

    return 0;
}

static boolean ChillPath_IsVictoryBossActor(objtype *ob)
{
    if(ob == NULL || !(ob->flags & FL_SHOOTABLE))
    {
        return false;
    }

    // Some boss maps, including Episode 2's Schabbs level, do not contain a
    // normal elevator or EXITTILE. The real game exits via the boss death-cam
    // sequence, so these bosses must count as standard exits for Any%.
    switch(ob->obclass)
    {
#ifndef SPEAR
        case schabbobj:
        case realhitlerobj:
        case giftobj:
        case fatobj:
            return true;
#else
        case angelobj:
            return true;
#endif

        default:
            break;
    }

    return false;
}

static boolean ChillPath_ActorDropsAmmo(objtype *ob)
{
    if(ob == NULL || !(ob->flags & FL_SHOOTABLE))
    {
        return false;
    }

    // These match KillActor(): guards/officers/mutants drop clips, and SS
    // drop either a machine gun or a clip depending on current weapon state.
    switch(ob->obclass)
    {
        case guardobj:
        case officerobj:
        case mutantobj:
        case ssobj:
            return true;

        default:
            break;
    }

    return false;
}

static boolean ChillPath_IsKeyItemNumber(int itemNumber)
{
    return itemNumber >= bo_key1 && itemNumber <= bo_key4;
}

static boolean ChillPath_IsTreasureItemNumber(int itemNumber)
{
    switch(itemNumber)
    {
        case bo_cross:
        case bo_chalice:
        case bo_bible:
        case bo_crown:
            return true;

        default:
            break;
    }

    return false;
}

static boolean ChillPath_IsAmmoItemNumber(int itemNumber)
{
    switch(itemNumber)
    {
        case bo_clip:
        case bo_clip2:
        case bo_25clip:
        case bo_machinegun:
        case bo_chaingun:
        case bo_fullheal:
            return true;

        default:
            break;
    }

    return false;
}

static boolean ChillPath_IsHealthItemNumber(int itemNumber)
{
    switch(itemNumber)
    {
        case bo_alpo:
        case bo_firstaid:
        case bo_food:
        case bo_fullheal:
        case bo_gibs:
            return true;

        default:
            break;
    }

    return false;
}

static boolean ChillPath_IsStatObjectActiveBonus(statobj_t *stat)
{
    if(stat == NULL)
    {
        return false;
    }

    if(stat->shapenum == -1)
    {
        return false;
    }

    if(!(stat->flags & FL_BONUS))
    {
        return false;
    }

    return ChillPath_InBounds(stat->tilex, stat->tiley);
}

static boolean ChillPath_HasStatItemAt(int tilex, int tiley, boolean (*predicate)(int))
{
    for(statobj_t *stat = &statobjlist[0]; stat != laststatobj; stat++)
    {
        if(!ChillPath_IsStatObjectActiveBonus(stat))
        {
            continue;
        }

        if(stat->tilex != tilex || stat->tiley != tiley)
        {
            continue;
        }

        if(predicate(stat->itemnumber))
        {
            return true;
        }
    }

    return false;
}

static void ChillPath_BuildKeyPickupTable(void)
{
    memset(ChillKeyMaskAt, 0, sizeof(ChillKeyMaskAt));

    for(statobj_t *stat = &statobjlist[0]; stat != laststatobj; stat++)
    {
        // Only real active bonus sprites can grant keys. Decorative statics
        // such as ceiling lights do not have meaningful itemnumber values;
        // treating them as pickups creates phantom keys and can make Any%
        // stop on ordinary floor tiles.
        if(!ChillPath_IsStatObjectActiveBonus(stat))
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

static boolean ChillPath_TileHasBlockingActorOrWall(int tilex, int tiley)
{
    if(!ChillPath_InBounds(tilex, tiley))
    {
        return true;
    }

    return actorat[tilex][tiley] ? true : false;
}

static boolean ChillPath_CanMoveOutOfPushedWallTile(int fromTileX, int fromTileY, int toTileX, int toTileY)
{
    if(!ChillPathfinder::IsPushableTile(fromTileX, fromTileY))
    {
        return true;
    }

    int moveDirection = ChillPath_DirectionFromTileToTile(fromTileX, fromTileY, toTileX, toTileY);

    if(moveDirection < 0)
    {
        return false;
    }

    int standTileX = fromTileX - ChillPathDx[moveDirection];
    int standTileY = fromTileY - ChillPathDy[moveDirection];

    // Moving in the same direction as the push enters the first tile behind
    // the pushwall. That only becomes walkable if the wall can complete its
    // second block of movement. If the second block is blocked, the wall stops
    // in the first tile behind the original pushwall.
    if(ChillPath_CanPushFromTile(fromTileX, fromTileY, standTileX, standTileY))
    {
        int finalTileX = fromTileX + ChillPathDx[moveDirection] * 2;
        int finalTileY = fromTileY + ChillPathDy[moveDirection] * 2;

        if(ChillPath_TileHasBlockingActorOrWall(finalTileX, finalTileY))
        {
            return false;
        }
    }

    return true;
}

static boolean ChillPath_IsBlockedByPushedWallFinalPosition(int fromTileX, int fromTileY, int toTileX, int toTileY)
{
    int moveDirection = ChillPath_DirectionFromTileToTile(fromTileX, fromTileY, toTileX, toTileY);

    if(moveDirection < 0)
    {
        return false;
    }

    int pushTileX = fromTileX - ChillPathDx[moveDirection];
    int pushTileY = fromTileY - ChillPathDy[moveDirection];
    int standTileX = pushTileX - ChillPathDx[moveDirection];
    int standTileY = pushTileY - ChillPathDy[moveDirection];

    if(!ChillPath_CanPushFromTile(pushTileX, pushTileY, standTileX, standTileY))
    {
        return false;
    }

    // If the final tile was originally clear, the pushwall eventually occupies
    // this tile after moving two blocks. The route may use the original wall
    // tile and the first tile behind it, but it cannot keep walking straight
    // through the final resting place of the wall.
    if(!ChillPath_TileHasBlockingActorOrWall(toTileX, toTileY))
    {
        return true;
    }

    return false;
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

    if(ChillPath_IsBlockedByPushedWallFinalPosition(fromTileX, fromTileY, toTileX, toTileY))
    {
        return false;
    }

    if(!ChillPath_CanMoveOutOfPushedWallTile(fromTileX, fromTileY, toTileX, toTileY))
    {
        return false;
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

    // Bit 0x40 is also used by the engine as a door-side marker on otherwise
    // empty floor. Real solid wall tile 64 is already rejected above because
    // actorat still contains a non-pointer wall marker there; an empty
    // door-side floor tile with value 64 must remain traversable.
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

static boolean ChillPath_IsElevatorSwitchTile(int tilex, int tiley)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[0] == NULL)
    {
        return false;
    }

    if(tilemap[tilex][tiley] != ELEVATORTILE)
    {
        return false;
    }

    // Avoid false exits from unrelated wall tiles that happen to have the same
    // texture number as the elevator switch. The original use code checks
    // tilemap only, but for pathfinding we must identify the real elevator
    // cluster from the loaded map data.
    return *(mapsegs[0] + (tiley << mapshift) + tilex) == ELEVATORTILE;
}

static boolean ChillPath_IsElevatorDoorIndex(int doorIndex)
{
    if(doorIndex < 0 || doorIndex >= doornum)
    {
        return false;
    }

    return doorobjlist[doorIndex].lock == dr_elevator;
}

static boolean ChillPath_IsElevatorDoorAt(int tilex, int tiley)
{
    int doorIndex = ChillPath_DoorIndexAt(tilex, tiley);
    return ChillPath_IsElevatorDoorIndex(doorIndex);
}

static boolean ChillPath_IsElevatorSwitchTileOnSide(int standTileX, int standTileY, int switchTileX, int switchTileY)
{
    if(!ChillPath_IsElevatorSwitchTile(switchTileX, switchTileY))
    {
        return false;
    }

    // Cmd_Use only accepts elevator switches when the player faces east/west.
    // Therefore the switch must be directly west or east of the stand tile.
    return switchTileY == standTileY && (switchTileX == standTileX - 1 || switchTileX == standTileX + 1);
}

static boolean ChillPath_IsStandForElevatorDoor(int tilex, int tiley)
{
    // Do not infer an elevator from a random floor tile near an elevator door.
    // A valid elevator stand must be geometrically tied to a real dr_elevator
    // door and to the switch wall the player can press with Cmd_Use().
    for(int i = 0; i < doornum; i++)
    {
        if(!ChillPath_IsElevatorDoorIndex(i))
        {
            continue;
        }

        doorobj_t *door = &doorobjlist[i];

        if(door->vertical)
        {
            // Vertical doors are crossed east/west. The elevator switch cluster
            // is on the chamber side, one tile beyond the stand tile.
            if(tiley != door->tiley)
            {
                continue;
            }

            if(tilex == door->tilex - 1)
            {
                if(ChillPath_IsElevatorSwitchTileOnSide(tilex, tiley, tilex - 1, tiley))
                {
                    return true;
                }
            }
            else if(tilex == door->tilex + 1)
            {
                if(ChillPath_IsElevatorSwitchTileOnSide(tilex, tiley, tilex + 1, tiley))
                {
                    return true;
                }
            }
        }
        else
        {
            // Horizontal elevator doors are rarer, but the use rule is still
            // east/west, so a valid stand beside the door must have a real
            // elevator switch immediately west or east of it.
            if(tilex != door->tilex)
            {
                continue;
            }

            if(tiley == door->tiley - 1 || tiley == door->tiley + 1)
            {
                if(ChillPath_IsElevatorSwitchTileOnSide(tilex, tiley, tilex - 1, tiley)
                    || ChillPath_IsElevatorSwitchTileOnSide(tilex, tiley, tilex + 1, tiley))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

static boolean ChillPath_IsVerifiedElevatorStandWithKeys(int tilex, int tiley, int keys, boolean secretExit)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[0] == NULL)
    {
        return false;
    }

    int offset = (tiley << mapshift) + tilex;
    int standTile = *(mapsegs[0] + offset);

    // Secret elevators are identified by the tile the player stands on, not by
    // a different switch. Any% must exclude those ALTELEVATORTILE stands.
    if(secretExit)
    {
        if(standTile != ALTELEVATORTILE)
        {
            return false;
        }
    }
    else
    {
        if(standTile == ALTELEVATORTILE)
        {
            return false;
        }
    }

    // The search only checks tiles it has reached, but this keeps the public
    // debug helpers honest and makes the rule explicit for locked/closed door
    // edge cases.
    if(!ChillPathfinder::IsTileTraversableWithKeys(tilex, tiley, keys))
    {
        return false;
    }

    if(!ChillPath_IsStandForElevatorDoor(tilex, tiley))
    {
        return false;
    }

    return true;
}

static boolean ChillPath_IsSecretElevatorStandWithKeys(int tilex, int tiley, int keys)
{
    return ChillPath_IsVerifiedElevatorStandWithKeys(tilex, tiley, keys, true);
}

static boolean ChillPath_IsStandardElevatorStandWithKeys(int tilex, int tiley, int keys)
{
    return ChillPath_IsVerifiedElevatorStandWithKeys(tilex, tiley, keys, false);
}

static boolean ChillPath_IsVictoryExitTileWithKeys(int tilex, int tiley, int keys)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[1] == NULL)
    {
        return false;
    }

    if(*(mapsegs[1] + (tiley << mapshift) + tilex) != EXITTILE)
    {
        return false;
    }

    // Victory exits are crossed by walking onto the tile.
    return ChillPathfinder::IsTileTraversableWithKeys(tilex, tiley, keys);
}

static boolean ChillPath_IsStandardExitTileWithKeys(int tilex, int tiley, int keys)
{
    if(ChillPath_IsVictoryExitTileWithKeys(tilex, tiley, keys))
    {
        return true;
    }

    return ChillPath_IsStandardElevatorStandWithKeys(tilex, tiley, keys);
}

static boolean ChillPath_IsSecretExitTileWithKeys(int tilex, int tiley, int keys)
{
    return ChillPath_IsSecretElevatorStandWithKeys(tilex, tiley, keys);
}

boolean ChillPathfinder::IsVerifiedStandardExitTile(int tilex, int tiley)
{
    return ChillPath_IsStandardExitTileWithKeys(tilex, tiley, gamestate.keys & (CHILL_PATHFIND_KEY_STATES - 1));
}

boolean ChillPathfinder::IsVerifiedSecretExitTile(int tilex, int tiley)
{
    return ChillPath_IsSecretExitTileWithKeys(tilex, tiley, gamestate.keys & (CHILL_PATHFIND_KEY_STATES - 1));
}

boolean ChillPathfinder::IsVerifiedVictoryExitTile(int tilex, int tiley)
{
    return ChillPath_IsVictoryExitTileWithKeys(tilex, tiley, gamestate.keys & (CHILL_PATHFIND_KEY_STATES - 1));
}

static boolean ChillPath_HasVictoryBossAt(int tilex, int tiley)
{
    for(objtype *ob = (objtype *)player->next; ob != NULL; ob = (objtype *)ob->next)
    {
        if(!ChillPath_IsVictoryBossActor(ob))
        {
            continue;
        }

        if(ob->tilex == tilex && ob->tiley == tiley)
        {
            return true;
        }
    }

    return false;
}

static boolean ChillPath_HasAmmoDropActorAt(int tilex, int tiley)
{
    for(objtype *ob = (objtype *)player->next; ob != NULL; ob = (objtype *)ob->next)
    {
        if(!ChillPath_ActorDropsAmmo(ob))
        {
            continue;
        }

        if(ob->tilex == tilex && ob->tiley == tiley)
        {
            return true;
        }
    }

    return false;
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
            if(ChillPath_IsStandardExitTileWithKeys(tilex, tiley, keys) || ChillPath_HasVictoryBossAt(tilex, tiley))
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
            if(ChillPath_IsSecretExitTileWithKeys(tilex, tiley, keys))
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

        case CHILL_SEARCH_AMMO:
            if(ChillPath_HasStatItemAt(tilex, tiley, ChillPath_IsAmmoItemNumber))
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

        case CHILL_SEARCH_AMMO_WITH_ENEMIES:
            if(ChillPath_HasStatItemAt(tilex, tiley, ChillPath_IsAmmoItemNumber) || ChillPath_HasAmmoDropActorAt(tilex, tiley))
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

        case CHILL_SEARCH_HEALTH:
            if(ChillPath_HasStatItemAt(tilex, tiley, ChillPath_IsHealthItemNumber))
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

static boolean ChillPath_TrimPathToFirstAction
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
        // The planner is allowed to use doors and pushwalls as part of a future
        // route, but the visible guide must not draw through an unopened
        // wall-like obstruction. Stop the current leg at the tile where the
        // player can stand and press use. After the obstruction opens, the
        // next frame will recalculate a fresh route through the new space.
        if(i > 0)
        {
            int actionTileX = path[i].tilex;
            int actionTileY = path[i].tiley;
            boolean stopAtAction = false;

            if(ChillPathfinder::IsPushableTile(actionTileX, actionTileY))
            {
                stopAtAction = true;
            }
            else
            {
                int doorIndex = ChillPath_DoorIndexAt(actionTileX, actionTileY);

                if(doorIndex >= 0)
                {
                    doorobj_t *door = &doorobjlist[doorIndex];

                    if(door->action != dr_open && door->action != dr_opening && doorposition[doorIndex] != 0xffff)
                    {
                        stopAtAction = true;
                    }
                }
            }

            if(stopAtAction)
            {
                *pathLength = i;

                if(targetTileX)
                {
                    *targetTileX = actionTileX;
                }

                if(targetTileY)
                {
                    *targetTileY = actionTileY;
                }

                return true;
            }
        }

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


static boolean ChillPath_FindFallbackPushableAction
(
    ChillPathPoint *path,
    int maxPathLength,
    int *pathLength,
    int *targetTileX,
    int *targetTileY,
    int startKeys
)
{
    if(player == NULL)
    {
        return false;
    }

    // If the requested objective is not currently reachable, the next useful
    // world interaction may still be a pushwall. This happens on maps where
    // the player begins in a room whose only exit is a pushable wall, or where
    // the route first requires opening a secret wall before any real objective
    // becomes reachable. We deliberately use the closest-pushable search here
    // because it only succeeds from a tile that can activate the wall using the
    // same side/direction rule as PushWall().
    boolean found = ChillPath_RunSearch
    (
        player->tilex,
        player->tiley,
        startKeys,
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

    if(found)
    {
        ChillPath_TrimPathToFirstAction(path, pathLength, startKeys, targetTileX, targetTileY);
    }

    return found;
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

    boolean found = ChillPath_RunSearch
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

    if(found)
    {
        // The closest-pushable search target is the wall tile itself, but the
        // player can only stand beside it and press use. Do not draw the floor
        // trail into the still-solid pushwall tile, because that looks like the
        // route goes straight through a wall.
        ChillPath_TrimPathToFirstAction(path, pathLength, gamestate.keys, targetTileX, targetTileY);
    }

    return found;
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
        ChillPath_TrimPathToFirstAction(path, pathLength, gamestate.keys, targetTileX, targetTileY);
        return true;
    }

    return ChillPath_FindFallbackPushableAction(path, maxPathLength, pathLength, targetTileX, targetTileY, gamestate.keys);
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
        ChillPath_TrimPathToFirstAction(path, pathLength, gamestate.keys, targetTileX, targetTileY);
        return true;
    }

    return ChillPath_FindFallbackPushableAction(path, maxPathLength, pathLength, targetTileX, targetTileY, gamestate.keys);
}


boolean ChillPathfinder::FindClosestAmmo
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
        CHILL_SEARCH_AMMO,
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
        ChillPath_TrimPathToFirstAction(path, pathLength, gamestate.keys, targetTileX, targetTileY);
        return true;
    }

    return ChillPath_FindFallbackPushableAction(path, maxPathLength, pathLength, targetTileX, targetTileY, gamestate.keys);
}

boolean ChillPathfinder::FindClosestAmmoWithEnemies
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
        CHILL_SEARCH_AMMO_WITH_ENEMIES,
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
        ChillPath_TrimPathToFirstAction(path, pathLength, gamestate.keys, targetTileX, targetTileY);
        return true;
    }

    return ChillPath_FindFallbackPushableAction(path, maxPathLength, pathLength, targetTileX, targetTileY, gamestate.keys);
}

boolean ChillPathfinder::FindClosestHealth
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
        CHILL_SEARCH_HEALTH,
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
        ChillPath_TrimPathToFirstAction(path, pathLength, gamestate.keys, targetTileX, targetTileY);
        return true;
    }

    return ChillPath_FindFallbackPushableAction(path, maxPathLength, pathLength, targetTileX, targetTileY, gamestate.keys);
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
        if(!ChillPath_IsStatObjectActiveBonus(stat))
        {
            continue;
        }

        if(ChillPath_IsKeyItemNumber(stat->itemnumber))
        {
            ChillPath_AddObjective(CHILL_OBJECTIVE_KEY, stat->tilex, stat->tiley, stat->tilex, stat->tiley);
        }
        else if(ChillPath_IsTreasureItemNumber(stat->itemnumber))
        {
            ChillPath_AddObjective(CHILL_OBJECTIVE_TREASURE, stat->tilex, stat->tiley, stat->tilex, stat->tiley);
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
            ChillObjectives[i].complete = true;
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
            ChillPath_TrimPathToFirstAction(path, pathLength, currentKeys, targetTileX, targetTileY);
            return true;
        }

        return ChillPath_FindFallbackPushableAction(path, maxPathLength, pathLength, targetTileX, targetTileY, currentKeys);
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
        ChillPath_TrimPathToFirstAction(path, pathLength, currentKeys, targetTileX, targetTileY);
        return true;
    }

    return ChillPath_FindFallbackPushableAction(path, maxPathLength, pathLength, targetTileX, targetTileY, currentKeys);
}

