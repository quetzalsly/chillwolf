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

    static boolean IsTileTraversable(int tilex, int tiley);
    static boolean IsPushableTile(int tilex, int tiley);

private:
    static boolean IsTileTraversableWithKeys(int tilex, int tiley, int keys);
};

#endif
