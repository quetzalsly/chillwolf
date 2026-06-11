//
// pathfind.h
// Chocolate Wolfenstein 3D
//
// Tile-aware pathfinding helpers for chill mode overlays.
//

#ifndef PATHFIND_H
#define PATHFIND_H

#include "wl_def.h"

#define CHILL_PATHFIND_MAX_PATH (MAPSIZE * MAPSIZE)

typedef struct
{
    int tilex;
    int tiley;
} ChillPathPoint;

class ChillPathfinder
{
public:
    static boolean FindClosestPushableTile
    (
        ChillPathPoint *path,
        int maxPathLength,
        int *pathLength,
        int *targetTileX,
        int *targetTileY
    );

    static boolean FindStandardExit
    (
        ChillPathPoint *path,
        int maxPathLength,
        int *pathLength,
        int *targetTileX,
        int *targetTileY
    );

    static boolean FindSecretExit
    (
        ChillPathPoint *path,
        int maxPathLength,
        int *pathLength,
        int *targetTileX,
        int *targetTileY
    );


    static boolean FindClosestAmmo
    (
        ChillPathPoint *path,
        int maxPathLength,
        int *pathLength,
        int *targetTileX,
        int *targetTileY
    );

    static boolean FindClosestAmmoWithEnemies
    (
        ChillPathPoint *path,
        int maxPathLength,
        int *pathLength,
        int *targetTileX,
        int *targetTileY
    );

    static boolean FindClosestHealth
    (
        ChillPathPoint *path,
        int maxPathLength,
        int *pathLength,
        int *targetTileX,
        int *targetTileY
    );

    static boolean FindHundredPercentPath
    (
        ChillPathPoint *path,
        int maxPathLength,
        int *pathLength,
        int *targetTileX,
        int *targetTileY
    );

    static boolean IsTileTraversable(int tilex, int tiley);
    static boolean IsPushableTile(int tilex, int tiley);

    static boolean IsVerifiedStandardExitTile(int tilex, int tiley);
    static boolean IsVerifiedSecretExitTile(int tilex, int tiley);
    static boolean IsVerifiedVictoryExitTile(int tilex, int tiley);

    static boolean IsTileTraversableWithKeys(int tilex, int tiley, int keys);
};

#endif
