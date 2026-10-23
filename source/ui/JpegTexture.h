#pragma once
#include <grrlib.h>

/* Decode a JPEG into an RGBA8 texture; nullptr on bad data or no memory.
 * maxSide > 0: a big one is decoded at a half, a quarter or an eighth of its
 * size (whichever first fits), so a photo costs little memory. */
GRRLIB_texImg* loadJPEGTexture(const u8* data, u32 size, u32 maxSide = 0);
