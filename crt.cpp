//
// crt.c
// Chocolate Wolfenstein 3D
//
// Created by fabien sanglard on 2014-08-26.
//
//

#include "crt.h"
#include "id_vl.h"

#include <stdio.h>
#include <stdlib.h>

static int displayWidth;
static int displayHeight;

static int viewportX;
static int viewportY;
static int viewportWidth;
static int viewportHeight;

static GLuint crtTexture = 0;

static unsigned char coloredFrameBuffer[320 * 200 * 3];

static void CRT_CalculateViewport(void)
{
    viewportWidth = displayWidth;
    viewportHeight = viewportWidth * 3 / 4;

    if(viewportHeight > displayHeight)
    {
        viewportHeight = displayHeight;
        viewportWidth = viewportHeight * 4 / 3;
    }

    viewportX = (displayWidth - viewportWidth) / 2;
    viewportY = (displayHeight - viewportHeight) / 2;
}

void CRT_Init(int width, int height)
{
    displayWidth = width;
    displayHeight = height;

    CRT_CalculateViewport();

    if(crtTexture != 0)
    {
        glDeleteTextures(1, &crtTexture);
        crtTexture = 0;
    }

    // Alloc the OpenGL texture where the screen will be uploaded each frame.
    glGenTextures(1, &crtTexture);
    glBindTexture(GL_TEXTURE_2D, crtTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexImage2D
    (
        GL_TEXTURE_2D,     // target
        0,                 // level, 0 = base, no minimap
        GL_RGB,            // internalformat
        320,               // width
        200,               // height
        0,                 // border
        GL_RGB,            // format
        GL_UNSIGNED_BYTE,  // type
        0
    );

    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, viewportWidth, 0, viewportHeight, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    SDL_GL_SwapBuffers();
}

void CRT_DAC(void)
{
    // Grab the screen framebuffer from SDL
    SDL_Surface *screen = screenBuffer;

    // Convert palette based framebuffer to RGB for OpenGL
    byte *pixelPointer = coloredFrameBuffer;

    for(int y = 0; y < 200; y++)
    {
        byte *source = ((byte *)screen->pixels) + y * screen->pitch;

        for(int x = 0; x < 320; x++)
        {
            unsigned char paletteIndex = source[x];

            *pixelPointer++ = curpal[paletteIndex].r;
            *pixelPointer++ = curpal[paletteIndex].g;
            *pixelPointer++ = curpal[paletteIndex].b;
        }
    }

    // Clear the full drawable first so letterbox/pillarbox bars stay black.
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw only into the centered 4:3 CRT viewport.
    glViewport(viewportX, viewportY, viewportWidth, viewportHeight);

    // Upload texture
    glBindTexture(GL_TEXTURE_2D, crtTexture);

    glTexSubImage2D
    (
        GL_TEXTURE_2D,
        0,
        0,
        0,
        320,
        200,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        coloredFrameBuffer
    );

    // Draw a quad with the texture
    glBegin(GL_QUADS);

    glTexCoord2f(0, 1);
    glVertex3i(0, 0, 0);

    glTexCoord2f(0, 0);
    glVertex3i(0, viewportHeight, 0);

    glTexCoord2f(1, 0);
    glVertex3i(viewportWidth, viewportHeight, 0);

    glTexCoord2f(1, 1);
    glVertex3i(viewportWidth, 0, 0);

    glEnd();

    // Flip buffer
    SDL_GL_SwapBuffers();

    Uint8 *keystate = SDL_GetKeyState(NULL);
    static int wasPressed = 0;

    if(keystate[SDLK_i])
    {
        if(!wasPressed)
        {
            wasPressed = 1;
            CRT_Screenshot();
        }
    }
    else
    {
        wasPressed = 0;
    }
}

void CRT_Screenshot(void)
{
    const char *filename = "screenshot.tga";

    printf("Screenshot.\n");

    // This prevents the images getting padded when the width multiplied by 3
    // is not a multiple of 4.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    int nSize = displayWidth * displayHeight * 3;

    // First let's create our buffer, 3 channels per Pixel
    char *dataBuffer = (char *)malloc(nSize * sizeof(char));

    if(!dataBuffer)
    {
        return;
    }

#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif

    // Let's fetch them from the backbuffer.
    // We request the pixels in GL_BGR format.
    glReadPixels
    (
        (GLint)0,
        (GLint)0,
        (GLint)displayWidth,
        (GLint)displayHeight,
        GL_BGR,
        GL_UNSIGNED_BYTE,
        dataBuffer
    );

    // Now the file creation
    FILE *filePtr = fopen(filename, "wb");

    if(!filePtr)
    {
        free(dataBuffer);
        return;
    }

    unsigned char TGAheader[12] =
    {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    unsigned char header[6] =
    {
        (unsigned char)(displayWidth % 256),
        (unsigned char)(displayWidth / 256),
        (unsigned char)(displayHeight % 256),
        (unsigned char)(displayHeight / 256),
        24,
        0
    };

    // We write the headers
    fwrite(TGAheader, sizeof(unsigned char), 12, filePtr);
    fwrite(header, sizeof(unsigned char), 6, filePtr);

    // And finally our image data
    fwrite(dataBuffer, sizeof(GLubyte), nSize, filePtr);

    fclose(filePtr);
    free(dataBuffer);
}
