//
// chill.h
// Chocolate Wolfenstein 3D
//
// Optional quality-of-life overlays.
//

#ifndef CHILL_H
#define CHILL_H

#include "wl_def.h"

//===========================================================================
// Chill mode controls
//===========================================================================

#define CHILL_HIGHLIGHT_MODE_KEY                 SDLK_COMMA
#define CHILL_PATHFIND_MODE_KEY                  SDLK_PERIOD

//===========================================================================
// Chill overlay text
//===========================================================================

#define CHILL_MESSAGE_TICS                       70
#define CHILL_TEXT_X                             4
#define CHILL_TEXT_Y                             4
#define CHILL_TEXT_R                             255
#define CHILL_TEXT_G                             255
#define CHILL_TEXT_B                             255
#define CHILL_TEXT_ALPHA                         255

//===========================================================================
// Pushable tile highlight
//===========================================================================

#define CHILL_PUSHABLE_HIGHLIGHT_R               0
#define CHILL_PUSHABLE_HIGHLIGHT_G               0
#define CHILL_PUSHABLE_HIGHLIGHT_B               255
#define CHILL_PUSHABLE_HIGHLIGHT_ALPHA           128

//===========================================================================
// Pathfind trail rendering
//===========================================================================

#define CHILL_PATH_GLOW_R                        0
#define CHILL_PATH_GLOW_G                        255
#define CHILL_PATH_GLOW_B                        40
#define CHILL_PATH_GLOW_ALPHA                    84

#define CHILL_PATH_TRAIL_R                       0
#define CHILL_PATH_TRAIL_G                       255
#define CHILL_PATH_TRAIL_B                       64
#define CHILL_PATH_TRAIL_ALPHA                   184

#define CHILL_PATH_ARROW_R                       150
#define CHILL_PATH_ARROW_G                       255
#define CHILL_PATH_ARROW_B                       150
#define CHILL_PATH_ARROW_ALPHA                   235

#define CHILL_PATH_ARROW_HOT_R                   230
#define CHILL_PATH_ARROW_HOT_G                   255
#define CHILL_PATH_ARROW_HOT_B                   230
#define CHILL_PATH_ARROW_HOT_ALPHA               250

#define CHILL_PATH_GLOW_HALF_WIDTH_GLOBAL        (TILEGLOBAL / 5)
#define CHILL_PATH_TRAIL_HALF_WIDTH_GLOBAL       (TILEGLOBAL / 13)
#define CHILL_PATH_ARROW_HALF_WIDTH_GLOBAL       (TILEGLOBAL / 4)
#define CHILL_PATH_ARROW_LENGTH_GLOBAL           (TILEGLOBAL / 2)
#define CHILL_PATH_ARROW_SPACING_GLOBAL          (TILEGLOBAL + (TILEGLOBAL / 3))
#define CHILL_PATH_ARROW_SPEED_GLOBAL_PER_TIC    (TILEGLOBAL / 36)
#define CHILL_PATH_MAX_DRAW_POINTS               512

typedef enum
{
    highlightmode_Off = 0,
    highlightmode_PushableTileHighlight,
    highlightmode_Count
} highlightmode_t;

typedef enum
{
    pathfindmode_Off = 0,
    pathfindmode_ClosestPushableTile,
    pathfindmode_AnyPercent,
    pathfindmode_SecretExit,
    pathfindmode_HundredPercent,
    pathfindmode_Count
} pathfindmode_t;

extern highlightmode_t HighlightMode;
extern pathfindmode_t PathfindMode;

extern const char *HighlightModeStrings[highlightmode_Count];
extern const char *PathfindModeStrings[pathfindmode_Count];

boolean Chill_OnKeyPress(int key);
boolean Chill_IsPushableTile(int tilex, int tiley);

void Chill_HookWallPost(byte *vidbuf, unsigned pitch, int screenx, int wallheight, boolean isPushableTile);
void Chill_HookWorld(byte *vidbuf, unsigned pitch);
void Chill_HookOverlay(void);

#endif
