#pragma once
#include <gctypes.h>

/* -----------------------------------------------------------------------
 * UsbMouse — USB mice, our own driver (after libogc's usbmouse.c, by
 * Daryl Borth, zlib licence; altered).
 *
 * libogc's driver only takes a "boot" mouse.  This one also reads any other
 * HID mouse through its report descriptor: the mouse part of a wireless
 * receiver (Logitech Lightspeed / Unifying), gaming mice.  A boot mouse is
 * still read in the boot protocol, as libogc did.  What it finds is logged
 * ("[Mouse] ...").
 * ----------------------------------------------------------------------- */
namespace UsbMouse {
    /* Starts its thread (it waits for a mouse); false without USB. */
    bool init();
    /* Before leaving WiiFin: the thread stopped, the device closed. */
    void shutdown();

    struct State {
        int dx, dy, wheel;    /* moved since the last take() */
        u8  pressed;          /* buttons pressed since then (bit 0 left, 1 right, 2 middle) */
        u8  held;             /* buttons down now */
    };
    State take();
    /* A mouse is plugged in and being read. */
    bool connected();
}
