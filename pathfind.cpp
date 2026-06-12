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
#define CHILL_PATHFIND_MAX_PUSHWALLS 12
#define CHILL_PATHFIND_PUSH_STATE_BASE 9
#define CHILL_PATHFIND_MAX_SEARCH_NODES 131072
#define CHILL_PATHFIND_VISITED_HASH_SIZE 262144

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
static boolean ChillPath_IsAdjacentToPushableTileWithState(int tilex, int tiley, unsigned long long pushState, int *targetTileX, int *targetTileY);
static boolean ChillPath_IsStandardExitTileWithKeysAndState(int tilex, int tiley, int keys, unsigned long long pushState);
static boolean ChillPath_IsSecretExitTileWithKeysAndState(int tilex, int tiley, int keys, unsigned long long pushState);
static boolean ChillPath_TryPushWallState(int standTileX, int standTileY, int pushTileX, int pushTileY, unsigned long long pushState, unsigned long long *newPushState);

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

typedef struct
{
    short tilex;
    short tiley;
    byte keys;
    unsigned long long pushState;
    int parent;
} ChillSearchNode;

static ChillSearchNode ChillSearchNodes[CHILL_PATHFIND_MAX_SEARCH_NODES];
static int ChillSearchQueue[CHILL_PATHFIND_MAX_SEARCH_NODES];
static unsigned long long ChillVisitedHashKeys[CHILL_PATHFIND_VISITED_HASH_SIZE];
static int ChillVisitedHashNodes[CHILL_PATHFIND_VISITED_HASH_SIZE];
static byte ChillVisitedHashUsed[CHILL_PATHFIND_VISITED_HASH_SIZE];

static short ChillPushWallX[CHILL_PATHFIND_MAX_PUSHWALLS];
static short ChillPushWallY[CHILL_PATHFIND_MAX_PUSHWALLS];
static unsigned long long ChillPushWallStatePower[CHILL_PATHFIND_MAX_PUSHWALLS];
static int ChillPushWallCount;

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

        case mechahitlerobj:
            // Episode 3's final map starts with Mecha-Hitler. Killing this
            // actor does not immediately set ex_victorious; it runs
            // A_HitlerMorph(), spawning realhitlerobj on the same tile. For
            // Any% this is still the required first boss interaction, because
            // there is no elevator/victory tile on the level.
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

static boolean ChillPath_HasUsableElevatorSwitchBeside(int tilex, int tiley)
{
    // Mirror Cmd_Use(): a level elevator is activated by using an
    // ELEVATORTILE directly east or west of the player's current tile.
    // Do not require the player to stand directly beside the elevator door;
    // several Wolf3D maps have a small elevator chamber where the usable
    // switch is a tile or two away from the dr_elevator door.
    return ChillPath_IsElevatorSwitchTileOnSide(tilex, tiley, tilex - 1, tiley)
        || ChillPath_IsElevatorSwitchTileOnSide(tilex, tiley, tilex + 1, tiley);
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

    if(!ChillPath_HasUsableElevatorSwitchBeside(tilex, tiley))
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
    unsigned long long pushState,
    int *targetTileX,
    int *targetTileY,
    int *objectiveIndex
)
{

    if(objectiveIndex)
    {
        *objectiveIndex = -1;
    }

    switch(searchMode)
    {
        case CHILL_SEARCH_CLOSEST_PUSHABLE:
            return ChillPath_IsAdjacentToPushableTileWithState(tilex, tiley, pushState, targetTileX, targetTileY);

        case CHILL_SEARCH_STANDARD_EXIT:
            if(ChillPath_IsStandardExitTileWithKeysAndState(tilex, tiley, keys, pushState) || ChillPath_HasVictoryBossAt(tilex, tiley))
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
            if(ChillPath_IsSecretExitTileWithKeysAndState(tilex, tiley, keys, pushState))
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

static void ChillPath_BuildPushWallStateTable(void)
{
    ChillPushWallCount = 0;
    unsigned long long power = 1;

    for(int y = 0; y < MAPSIZE; y++)
    {
        for(int x = 0; x < MAPSIZE; x++)
        {
            if(!ChillPathfinder::IsPushableTile(x, y))
            {
                continue;
            }

            if(tilemap[x][y] == 0)
            {
                continue;
            }

            if(ChillPushWallCount >= CHILL_PATHFIND_MAX_PUSHWALLS)
            {
                continue;
            }

            ChillPushWallX[ChillPushWallCount] = (short)x;
            ChillPushWallY[ChillPushWallCount] = (short)y;
            ChillPushWallStatePower[ChillPushWallCount] = power;
            ChillPushWallCount++;
            power *= CHILL_PATHFIND_PUSH_STATE_BASE;
        }
    }
}

static int ChillPath_PushWallIndexAt(int tilex, int tiley)
{
    for(int i = 0; i < ChillPushWallCount; i++)
    {
        if(ChillPushWallX[i] == tilex && ChillPushWallY[i] == tiley)
        {
            return i;
        }
    }

    return -1;
}

static int ChillPath_GetPushWallStateDigit(unsigned long long pushState, int pushWallIndex)
{
    if(pushWallIndex < 0 || pushWallIndex >= ChillPushWallCount)
    {
        return 0;
    }

    return (int)((pushState / ChillPushWallStatePower[pushWallIndex]) % CHILL_PATHFIND_PUSH_STATE_BASE);
}

static unsigned long long ChillPath_SetPushWallStateDigit(unsigned long long pushState, int pushWallIndex, int digit)
{
    int oldDigit = ChillPath_GetPushWallStateDigit(pushState, pushWallIndex);

    return pushState + ((unsigned long long)(digit - oldDigit) * ChillPushWallStatePower[pushWallIndex]);
}

static boolean ChillPath_DecodePushedWall(int pushWallIndex, unsigned long long pushState, int *originX, int *originY, int *finalX, int *finalY)
{
    int digit = ChillPath_GetPushWallStateDigit(pushState, pushWallIndex);

    if(digit <= 0)
    {
        return false;
    }

    int direction = (digit - 1) & 3;
    int distance = (digit >= 5) ? 2 : 1;

    int x = ChillPushWallX[pushWallIndex];
    int y = ChillPushWallY[pushWallIndex];

    if(originX)
    {
        *originX = x;
    }

    if(originY)
    {
        *originY = y;
    }

    if(finalX)
    {
        *finalX = x + ChillPathDx[direction] * distance;
    }

    if(finalY)
    {
        *finalY = y + ChillPathDy[direction] * distance;
    }

    return true;
}

static boolean ChillPath_IsOpenedPushWallOrigin(int tilex, int tiley, unsigned long long pushState)
{
    int index = ChillPath_PushWallIndexAt(tilex, tiley);

    if(index < 0)
    {
        return false;
    }

    return ChillPath_GetPushWallStateDigit(pushState, index) > 0;
}

static boolean ChillPath_IsPushedWallFinalTile(int tilex, int tiley, unsigned long long pushState)
{
    for(int i = 0; i < ChillPushWallCount; i++)
    {
        int finalX;
        int finalY;

        if(!ChillPath_DecodePushedWall(i, pushState, NULL, NULL, &finalX, &finalY))
        {
            continue;
        }

        if(finalX == tilex && finalY == tiley)
        {
            return true;
        }
    }

    return false;
}

static boolean ChillPath_ActorAtWithPushState(int tilex, int tiley, unsigned long long pushState)
{
    if(!ChillPath_InBounds(tilex, tiley))
    {
        return true;
    }

    if(ChillPath_IsPushedWallFinalTile(tilex, tiley, pushState))
    {
        return true;
    }

    if(ChillPath_IsOpenedPushWallOrigin(tilex, tiley, pushState))
    {
        return false;
    }

    return actorat[tilex][tiley] ? true : false;
}

static boolean ChillPath_BlockingStaticAtWithPushState(int tilex, int tiley, unsigned long long pushState)
{
    if(ChillPath_IsOpenedPushWallOrigin(tilex, tiley, pushState))
    {
        return false;
    }

    if(ChillPath_IsPushedWallFinalTile(tilex, tiley, pushState))
    {
        return true;
    }

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

static boolean ChillPath_IsTileTraversableWithKeysAndPushState(int tilex, int tiley, int keys, unsigned long long pushState)
{
    if(!ChillPath_InBounds(tilex, tiley))
    {
        return false;
    }

    if(ChillPath_IsPushedWallFinalTile(tilex, tiley, pushState))
    {
        return false;
    }

    if(ChillPathfinder::IsPushableTile(tilex, tiley))
    {
        return ChillPath_IsOpenedPushWallOrigin(tilex, tiley, pushState);
    }

    byte tile = tilemap[tilex][tiley];

    if(tile & 0x80)
    {
        return ChillPath_CanPassDoor(tile & 0x7f, keys);
    }

    if(ChillPath_IsActivePushwallTile(tilex, tiley))
    {
        return false;
    }

    if(ChillPath_BlockingStaticAtWithPushState(tilex, tiley, pushState))
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

static boolean ChillPath_IsVerifiedElevatorStandWithKeysAndState(int tilex, int tiley, int keys, boolean secretExit, unsigned long long pushState)
{
    if(!ChillPath_InBounds(tilex, tiley) || mapsegs[0] == NULL)
    {
        return false;
    }

    int offset = (tiley << mapshift) + tilex;
    int standTile = *(mapsegs[0] + offset);

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

    if(!ChillPath_IsTileTraversableWithKeysAndPushState(tilex, tiley, keys, pushState))
    {
        return false;
    }

    if(!ChillPath_HasUsableElevatorSwitchBeside(tilex, tiley))
    {
        return false;
    }

    return true;
}

static boolean ChillPath_IsStandardExitTileWithKeysAndState(int tilex, int tiley, int keys, unsigned long long pushState)
{
    if(ChillPath_IsVictoryExitTileWithKeys(tilex, tiley, keys))
    {
        return true;
    }

    return ChillPath_IsVerifiedElevatorStandWithKeysAndState(tilex, tiley, keys, false, pushState);
}

static boolean ChillPath_IsSecretExitTileWithKeysAndState(int tilex, int tiley, int keys, unsigned long long pushState)
{
    return ChillPath_IsVerifiedElevatorStandWithKeysAndState(tilex, tiley, keys, true, pushState);
}

static boolean ChillPath_IsAdjacentToPushableTileWithState(int tilex, int tiley, unsigned long long pushState, int *targetTileX, int *targetTileY)
{
    for(int i = 0; i < 4; i++)
    {
        int checkx = tilex + ChillPathDx[i];
        int checky = tiley + ChillPathDy[i];
        int pushWallIndex = ChillPath_PushWallIndexAt(checkx, checky);

        if(pushWallIndex < 0 || ChillPath_GetPushWallStateDigit(pushState, pushWallIndex) != 0)
        {
            continue;
        }

        if(ChillPath_TryPushWallState(tilex, tiley, checkx, checky, pushState, NULL))
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

static unsigned int ChillPath_HashSearchKey(unsigned long long key)
{
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdULL;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ULL;
    key ^= key >> 33;

    return (unsigned int)key & (CHILL_PATHFIND_VISITED_HASH_SIZE - 1);
}

static unsigned long long ChillPath_MakeSearchKey(int tilex, int tiley, int keys, unsigned long long pushState)
{
    unsigned long long key = pushState;

    key = key * CHILL_PATHFIND_KEY_STATES + (unsigned long long)(keys & (CHILL_PATHFIND_KEY_STATES - 1));
    key = key * MAPSIZE + (unsigned long long)tilex;
    key = key * MAPSIZE + (unsigned long long)tiley;
    key++;

    return key;
}

static boolean ChillPath_FindVisitedNode(int tilex, int tiley, int keys, unsigned long long pushState, int *nodeIndex)
{
    unsigned long long key = ChillPath_MakeSearchKey(tilex, tiley, keys, pushState);
    unsigned int slot = ChillPath_HashSearchKey(key);

    for(int probe = 0; probe < CHILL_PATHFIND_VISITED_HASH_SIZE; probe++)
    {
        if(!ChillVisitedHashUsed[slot])
        {
            return false;
        }

        if(ChillVisitedHashKeys[slot] == key)
        {
            if(nodeIndex)
            {
                *nodeIndex = ChillVisitedHashNodes[slot];
            }

            return true;
        }

        slot = (slot + 1) & (CHILL_PATHFIND_VISITED_HASH_SIZE - 1);
    }

    return false;
}

static boolean ChillPath_AddVisitedNode(int nodeIndex)
{
    ChillSearchNode *node = &ChillSearchNodes[nodeIndex];
    unsigned long long key = ChillPath_MakeSearchKey(node->tilex, node->tiley, node->keys, node->pushState);
    unsigned int slot = ChillPath_HashSearchKey(key);

    for(int probe = 0; probe < CHILL_PATHFIND_VISITED_HASH_SIZE; probe++)
    {
        if(!ChillVisitedHashUsed[slot])
        {
            ChillVisitedHashUsed[slot] = 1;
            ChillVisitedHashKeys[slot] = key;
            ChillVisitedHashNodes[slot] = nodeIndex;
            return true;
        }

        if(ChillVisitedHashKeys[slot] == key)
        {
            return false;
        }

        slot = (slot + 1) & (CHILL_PATHFIND_VISITED_HASH_SIZE - 1);
    }

    return false;
}

static int ChillPath_AppendSearchNode(int tilex, int tiley, int keys, unsigned long long pushState, int parent, int *tail)
{
    if(*tail >= CHILL_PATHFIND_MAX_SEARCH_NODES)
    {
        return -1;
    }

    int existingNode;

    if(ChillPath_FindVisitedNode(tilex, tiley, keys, pushState, &existingNode))
    {
        return -1;
    }

    int nodeIndex = *tail;

    ChillSearchNodes[nodeIndex].tilex = (short)tilex;
    ChillSearchNodes[nodeIndex].tiley = (short)tiley;
    ChillSearchNodes[nodeIndex].keys = (byte)(keys & (CHILL_PATHFIND_KEY_STATES - 1));
    ChillSearchNodes[nodeIndex].pushState = pushState;
    ChillSearchNodes[nodeIndex].parent = parent;

    if(!ChillPath_AddVisitedNode(nodeIndex))
    {
        return -1;
    }

    ChillSearchQueue[*tail] = nodeIndex;
    (*tail)++;

    return nodeIndex;
}

static boolean ChillPath_IsBlockingForPushState(int tilex, int tiley, unsigned long long pushState)
{
    if(!ChillPath_InBounds(tilex, tiley))
    {
        return true;
    }

    return ChillPath_ActorAtWithPushState(tilex, tiley, pushState);
}

static boolean ChillPath_TryPushWallState(int standTileX, int standTileY, int pushTileX, int pushTileY, unsigned long long pushState, unsigned long long *newPushState)
{
    int pushWallIndex = ChillPath_PushWallIndexAt(pushTileX, pushTileY);

    if(pushWallIndex < 0)
    {
        return false;
    }

    if(ChillPath_GetPushWallStateDigit(pushState, pushWallIndex) != 0)
    {
        return false;
    }

    int pushDirection = ChillPath_DirectionFromTileToTile(standTileX, standTileY, pushTileX, pushTileY);

    if(pushDirection < 0)
    {
        return false;
    }

    int firstTileX = pushTileX + ChillPathDx[pushDirection];
    int firstTileY = pushTileY + ChillPathDy[pushDirection];

    if(ChillPath_IsBlockingForPushState(firstTileX, firstTileY, pushState))
    {
        return false;
    }

    int secondTileX = pushTileX + ChillPathDx[pushDirection] * 2;
    int secondTileY = pushTileY + ChillPathDy[pushDirection] * 2;
    int distance = 2;

    if(ChillPath_IsBlockingForPushState(secondTileX, secondTileY, pushState))
    {
        distance = 1;
    }

    int digit = 1 + pushDirection;

    if(distance == 2)
    {
        digit += 4;
    }

    if(newPushState)
    {
        *newPushState = ChillPath_SetPushWallStateDigit(pushState, pushWallIndex, digit);
    }

    return true;
}

static boolean ChillPath_CanMoveBetweenTilesWithPushState(int fromTileX, int fromTileY, int toTileX, int toTileY, int keys, unsigned long long pushState)
{
    if(!ChillPath_InBounds(fromTileX, fromTileY) || !ChillPath_InBounds(toTileX, toTileY))
    {
        return false;
    }

    if(!ChillPath_IsTileTraversableWithKeysAndPushState(toTileX, toTileY, keys, pushState))
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

static boolean ChillPath_CopySparseSearchPath
(
    int foundNode,
    ChillPathPoint *path,
    int maxPathLength,
    int *pathLength,
    int targetTileX,
    int targetTileY
)
{
    int reverseLength = 0;
    int walkIndex = foundNode;

    while(walkIndex >= 0 && reverseLength < CHILL_PATHFIND_MAX_PATH)
    {
        ChillSearchNode *node = &ChillSearchNodes[walkIndex];

        ChillReversePath[reverseLength].tilex = node->tilex;
        ChillReversePath[reverseLength].tiley = node->tiley;
        reverseLength++;
        walkIndex = node->parent;
    }

    int outputLength = 0;

    for(int i = reverseLength - 1; i >= 0 && outputLength < maxPathLength; i--)
    {
        if(outputLength > 0 && path[outputLength - 1].tilex == ChillReversePath[i].tilex && path[outputLength - 1].tiley == ChillReversePath[i].tiley)
        {
            continue;
        }

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
    ChillPath_BuildPushWallStateTable();
    memset(ChillVisitedHashUsed, 0, sizeof(ChillVisitedHashUsed));

    int initialKeys = startKeys & (CHILL_PATHFIND_KEY_STATES - 1);
    int head = 0;
    int tail = 0;

    if(ChillPath_AppendSearchNode(startx, starty, initialKeys, 0, -1, &tail) < 0)
    {
        return false;
    }

    int foundNode = -1;
    int foundTargetTileX = -1;
    int foundTargetTileY = -1;
    int foundObjectiveIndex = -1;
    int foundKeys = initialKeys;

    while(head < tail)
    {
        int nodeIndex = ChillSearchQueue[head++];
        ChillSearchNode *node = &ChillSearchNodes[nodeIndex];
        int tilex = node->tilex;
        int tiley = node->tiley;
        int keys = node->keys;
        unsigned long long pushState = node->pushState;

        if(ChillPath_SearchTarget(searchMode, tilex, tiley, keys, pushState, &foundTargetTileX, &foundTargetTileY, &foundObjectiveIndex))
        {
            foundNode = nodeIndex;
            foundKeys = keys;
            break;
        }

        for(int i = 0; i < 4; i++)
        {
            int nextx = tilex + ChillPathDx[i];
            int nexty = tiley + ChillPathDy[i];

            if(searchMode != CHILL_SEARCH_CLOSEST_PUSHABLE)
            {
                unsigned long long pushedState;

                if(ChillPath_TryPushWallState(tilex, tiley, nextx, nexty, pushState, &pushedState))
                {
                    ChillPath_AppendSearchNode(tilex, tiley, keys, pushedState, nodeIndex, &tail);
                }
            }

            if(!ChillPath_CanMoveBetweenTilesWithPushState(tilex, tiley, nextx, nexty, keys, pushState))
            {
                continue;
            }

            int nextKeys = ChillPath_KeysAfterEnteringTile(nextx, nexty, keys);
            ChillPath_AppendSearchNode(nextx, nexty, nextKeys, pushState, nodeIndex, &tail);
        }
    }

    if(foundNode < 0)
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
        *endTileX = ChillSearchNodes[foundNode].tilex;
    }

    if(endTileY)
    {
        *endTileY = ChillSearchNodes[foundNode].tiley;
    }

    if(endKeys)
    {
        *endKeys = foundKeys;
    }

    if(objectiveIndex)
    {
        *objectiveIndex = foundObjectiveIndex;
    }

    return ChillPath_CopySparseSearchPath(foundNode, path, maxPathLength, pathLength, foundTargetTileX, foundTargetTileY);
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


    return found;
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


    return found;
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


    return found;
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

    return foundExit;
}

