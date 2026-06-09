//
// chill.h
// Chocolate Wolfenstein 3D
//
// Optional "chill" quality-of-life overlays.
//

#ifndef CHILL_H
#define CHILL_H

#include "wl_def.h"

typedef enum
{
    chillmode_Off = 0,
    chillmode_PushableTileHighlight,
    chillmode_Count
} chillmode_t;

extern chillmode_t ChillMode;
extern const char *ChillModeStrings[chillmode_Count];

boolean Chill_OnKeyPress(int key);
boolean Chill_IsPushableTile(int tilex, int tiley);

void Chill_HookWorld(byte *vidbuf, unsigned pitch, int screenx, int wallheight, boolean isPushableTile);
void Chill_HookOverlay(void);

#endif
