//
// crt.h
// Chocolate Wolfenstein 3D
//
// Created by fabien sanglard on 2014-08-26.
//
//

#ifndef Chocolate_Wolfenstein_3D_crt_h
#define Chocolate_Wolfenstein_3D_crt_h

// Win32
#ifdef _WIN32
#include <windows.h>
#include <gl/gl.h>
#include "SDL.h"
#elif __linux__
#include <GL/gl.h>
#include "SDL/SDL.h"
#else
#include <OpenGL/gl.h>
#include "SDL/SDL.h"
#endif

extern SDL_Color curpal[256];

/*
 * CRT aspect ratio is 4:3. The raw game framebuffer remains 320x200,
 * but the OpenGL presentation viewport is fitted to 4:3.
 */
void CRT_Init(int width, int height);

/*
 * Trigger the Digital To Analogic convertion
 */
void CRT_DAC(void);

void CRT_Screenshot(void);

#endif
