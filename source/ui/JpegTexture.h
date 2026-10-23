#pragma once
#include <grrlib.h>

/* Decode a JPEG into an RGBA8 texture; nullptr on bad data or no memory.
 * maxSide > 0: a big one is decoded at a half, a quarter or an eighth of its
 * size (whichever first fits), so a photo costs little memory. */
GRRLIB_texImg* loadJPEGTexture(const u8* data, u32 size, u32 maxSide = 0);

/* tex made at most maxSide across and down (each pixel the average of a few
 * under it, sides a multiple of 4), tex freed.  Small enough already, or no
 * memory for the new one: tex itself. */
GRRLIB_texImg* shrinkTexture(GRRLIB_texImg* tex, u32 maxSide);
