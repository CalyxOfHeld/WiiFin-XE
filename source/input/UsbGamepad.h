#pragma once
#include <gctypes.h>

/* -----------------------------------------------------------------------
 * UsbGamepad — USB HID Gamepad driver for Nintendo Wii.
 * Supports wired Sony DualShock 4/3, Microsoft Xbox One/360,
 * Nintendo Switch Pro, and generic DirectInput USB gamepads.
 * ----------------------------------------------------------------------- */
namespace UsbGamepad {
    /* Starts background worker thread; returns true if USB stack is up. */
    bool init();
    /* Stops thread and closes USB device on app shutdown. */
    void shutdown();

    struct State {
        u32  pressed;       /* newly pressed buttons this frame */
        u32  held;          /* currently held buttons */
        s8   stickX;        /* left analog stick X: -128..127 */
        s8   stickY;        /* left analog stick Y: -128..127 (positive = up) */
        s8   subX;          /* right analog stick X: -128..127 */
        s8   subY;          /* right analog stick Y: -128..127 (positive = up) */
        bool connected;     /* controller actively connected and streaming */
    };

    /* Collects state updates since last call */
    State take();
    bool  connected();
    bool  isPlayStation();
    bool  isXbox();
    /* A USB device one of the pads is reading; UsbMouse leaves it alone. */
    bool  owns(s32 device);
}
