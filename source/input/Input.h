#pragma once
#include <wiiuse/wpad.h>

/* -----------------------------------------------------------------------
 * Input — every controller, read as a Wii Remote.
 *
 * The Classic Controller (also what the Wii U GamePad is in a Virtual
 * Console inject) and GameCube controllers (all four ports, all four Wii
 * Remotes too: any of them drives) are translated to
 * Wii Remote buttons, so screens only test WPAD_BUTTON_* bits:
 *
 *   Wii Remote   Classic / GamePad          GameCube
 *   A / B        A / B                      A / B
 *   D-pad        D-pad, left stick          D-pad, stick, C-Stick
 *   - / +        - / +, L / R               L / R
 *   1 / 2        Y / X                      Y / X
 *   HOME         HOME                       START
 *   (zoom)       ZL / ZR                    Z         -> BTN_ZOOM
 *
 * The is*Pressed() directions repeat while held (menus, lists, keyboard);
 * rawDown() does not (the player: a held D-pad must not seek again and
 * again).  No pointer without a Wii Remote: screens fall back to the
 * D-pad when ir.valid is false.  A USB mouse is a pointer too: left click
 * A, right click B, the wheel Up / Down.  A USB keyboard has fixed keys:
 *
 *   arrows D-pad, Enter A, Esc B, F1 / F2 1 / 2, Page Up / Down - / +,
 *   Home HOME, F3 zoom
 * ----------------------------------------------------------------------- */
class Input {
public:
    /* Video zoom (Fit / Fill), no Wii Remote button for it */
    static const u32 BTN_ZOOM = 1u << 30;

    /* Dedicated virtual buttons for specific controller axes */
    static const u32 BTN_DPAD_UP     = 1u << 16;
    static const u32 BTN_DPAD_DOWN   = 1u << 17;
    static const u32 BTN_DPAD_LEFT   = 1u << 18;
    static const u32 BTN_DPAD_RIGHT  = 1u << 19;

    static const u32 BTN_C_UP        = 1u << 20;
    static const u32 BTN_C_DOWN      = 1u << 21;
    static const u32 BTN_C_LEFT      = 1u << 22;
    static const u32 BTN_C_RIGHT     = 1u << 23;

    static const u32 BTN_STICK_UP    = 1u << 24;
    static const u32 BTN_STICK_DOWN  = 1u << 25;
    static const u32 BTN_STICK_LEFT  = 1u << 26;
    static const u32 BTN_STICK_RIGHT = 1u << 27;

    enum class PadScrub { CStick = 0, MainStick = 1, DPad = 2, Off = 3 };
    enum class PadVolume { DPad = 0, MainStick = 1, CStick = 2, Off = 3 };

    static PadScrub padScrub();
    static void setPadScrub(PadScrub s);
    static PadVolume padVolume();
    static void setPadVolume(PadVolume v);

    static void update();
    /* Wii Remote pointer in drawing space (see Ui::pointerToScreen). */
    static void readIR(ir_t& ir);
    /* The Wii Remote whose pointer that is (the one used last). */
    static int pointerChan();
    /* The pointer is the USB mouse's (it draws as an arrow). */
    static bool pointerIsMouse();
    /* The pointer's tilt (its drawing): the Wii Remote's, none for a mouse. */
    static float pointerRoll();
    /* Before leaving WiiFin: the USB keyboard and mouse stopped. */
    static void shutdown();
    /* What was typed on a USB keyboard this frame, one character a call
     * ('\n' Enter, '\b' Backspace / Delete), 0 when no more. */
    static int typedChar();

    /* This frame's presses dropped: a screen that closed with them (the
     * HOME menu's B) leaves none for the one under it.  Held stays. */
    static void consume();

    /* Buttons pressed this frame, Wii Remote bits, no repeat. */
    static u32 rawDown();
    /* Buttons held, Wii Remote bits. */
    static u32 held();

    static bool isHomePressed();
    static bool isUpPressed();
    static bool isDownPressed();
    static bool isLeftPressed();
    static bool isRightPressed();
    static bool isAJustPressed();
    static bool isBPressed();
    static bool isBackPressed();  // alias for B
    static bool isLPressed();     // -
    static bool isRPressed();     // +
    /* + as an action of its own (Watched, Browse, the keyboard's Enter), not
     * the next page: the + buttons and Z (ZL / ZR), not the R triggers,
     * which only turn pages (hints: "+!") */
    static bool isActionPressed();
    static bool is1Pressed();
    static bool is2Pressed();
    /* GameCube controller buttons pressed this frame (PAD_*, any port;
     * 0: none): for the few places its own layout counts (the keyboard) */
    static u16  gcRawDown();
};
