#include "Input.h"
#include "../ui/Ui.h"
#include <wiiuse/wpad.h>
#include "wupc.h"
#include "UsbGamepad.h"
#include <ogc/pad.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>
#include <wiikeyboard/keyboard.h>
#include "UsbMouse.h"
#include "../core/Screensaver.h"
#include <math.h>
#include <string.h>

static u32 rawDownBits = 0;   /* pressed this frame, no repeat */
static u32 heldBits    = 0;
static u32 buttonsDown = 0;   /* rawDownBits + repeated directions */
static bool actionDown = false;   /* isActionPressed */
static u16  s_gcRawDown = 0;      /* gcRawDown */

static Input::PadScrub  s_padScrub  = Input::PadScrub::CStick;
static Input::PadVolume s_padVolume = Input::PadVolume::DPad;

Input::PadScrub Input::padScrub() { return s_padScrub; }
void Input::setPadScrub(Input::PadScrub s) { s_padScrub = s; }

Input::PadVolume Input::padVolume() { return s_padVolume; }
void Input::setPadVolume(Input::PadVolume v) { s_padVolume = v; }

static const u32 DIRS = WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT;

/* Classic Controller bits (upper half of the WPAD word) -> Wii Remote bits */
static u32 fromClassic(u32 c)
{
    static const struct { u32 from, to; } MAP[] = {
        { WPAD_CLASSIC_BUTTON_UP,    WPAD_BUTTON_UP | Input::BTN_DPAD_UP },
        { WPAD_CLASSIC_BUTTON_DOWN,  WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN },
        { WPAD_CLASSIC_BUTTON_LEFT,  WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT },
        { WPAD_CLASSIC_BUTTON_RIGHT, WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT },
        { WPAD_CLASSIC_BUTTON_A,     WPAD_BUTTON_A },
        { WPAD_CLASSIC_BUTTON_B,     WPAD_BUTTON_B },
        { WPAD_CLASSIC_BUTTON_Y,     WPAD_BUTTON_1 },
        { WPAD_CLASSIC_BUTTON_X,     WPAD_BUTTON_2 },
        { WPAD_CLASSIC_BUTTON_MINUS, WPAD_BUTTON_MINUS },
        { WPAD_CLASSIC_BUTTON_PLUS,  WPAD_BUTTON_PLUS },
        { WPAD_CLASSIC_BUTTON_FULL_L, WPAD_BUTTON_MINUS },
        { WPAD_CLASSIC_BUTTON_FULL_R, WPAD_BUTTON_PLUS },
        { WPAD_CLASSIC_BUTTON_HOME,  WPAD_BUTTON_HOME },
        { WPAD_CLASSIC_BUTTON_ZL,    Input::BTN_ZOOM },
        { WPAD_CLASSIC_BUTTON_ZR,    Input::BTN_ZOOM },
    };
    u32 out = 0;
    for (const auto& m : MAP) if (c & m.from) out |= m.to;
    return out;
}

/* A Nunchuk's buttons, as a Classic Controller's shoulder buttons: C is
 * -, Z is + (tabs, pages) */
static u32 fromNunchuk(u32 n)
{
    return ((n & WPAD_NUNCHUK_BUTTON_C) ? WPAD_BUTTON_MINUS : 0) |
           ((n & WPAD_NUNCHUK_BUTTON_Z) ? WPAD_BUTTON_PLUS  : 0);
}

/* The upper half holds the expansion's buttons, the Classic Controller's
 * or the Nunchuk's (the same bits, read by what is plugged in). */
static u32 fromWpad(u32 w, bool classic, bool nunchuk)
{
    u32 base = w & 0xFFFF;
    u32 dpad = 0;
    if (base & WPAD_BUTTON_UP)    dpad |= Input::BTN_DPAD_UP;
    if (base & WPAD_BUTTON_DOWN)  dpad |= Input::BTN_DPAD_DOWN;
    if (base & WPAD_BUTTON_LEFT)  dpad |= Input::BTN_DPAD_LEFT;
    if (base & WPAD_BUTTON_RIGHT) dpad |= Input::BTN_DPAD_RIGHT;

    return base | dpad | (classic ? fromClassic(w & 0xFFFF0000u) : 0)
                       | (nunchuk ? fromNunchuk(w & 0xFFFF0000u) : 0);
}

static u32 fromGameCube(u16 p)
{
    static const struct { u16 from; u32 to; } MAP[] = {
        { PAD_BUTTON_UP,    WPAD_BUTTON_UP | Input::BTN_DPAD_UP },
        { PAD_BUTTON_DOWN,  WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN },
        { PAD_BUTTON_LEFT,  WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT },
        { PAD_BUTTON_RIGHT, WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT },
        { PAD_BUTTON_A,     WPAD_BUTTON_A },
        { PAD_BUTTON_B,     WPAD_BUTTON_B },
        { PAD_BUTTON_Y,     WPAD_BUTTON_1 },
        { PAD_BUTTON_X,     WPAD_BUTTON_2 },
        { PAD_TRIGGER_L,    WPAD_BUTTON_MINUS },
        { PAD_TRIGGER_R,    WPAD_BUTTON_PLUS },
        { PAD_BUTTON_START, WPAD_BUTTON_HOME },
        { PAD_TRIGGER_Z,    Input::BTN_ZOOM },
    };
    u32 out = 0;
    for (const auto& m : MAP) if (p & m.from) out |= m.to;
    return out;
}

/* GameCube controller state (each port), with our own press detection:
 * when a read gets no answer, libogc clears the pad's state, and a button
 * still held came back as a new press at the next answer (about every
 * 130 ms in Dolphin): one A on "Connect To Jellyfin" also picked the first
 * profile.  A read without an answer now keeps the last state. */
static const int PORTS = 4;                    /* GameCube ports, Wii Remotes */
static u16  s_padHeld[PORTS] = {};
static bool s_padUp[PORTS]   = {};            /* last read answered */
static int  s_pointerChan    = WPAD_CHAN_0;   /* the Wii Remote used last */

/* A USB mouse (UsbMouse: wired ones, wireless receivers): the pointer while
 * it is the one moving, left click A, right click B, the wheel Up / Down.
 * A Wii Remote pointing or pressed, or a key, takes the pointer back. */
static int   s_mouse   = 0;                   /* 0 not tried, 1 driver up, -1 none */
static bool  s_mouseOn = false;               /* the mouse has the pointer */
static float s_mouseX  = 320, s_mouseY = 240; /* pointer space, 640 x 480 */
static bool  s_mouseMoved = false;            /* this frame: readIR re-anchors the Wii Remote */
static bool  s_pointerMoved = false;          /* readIR: the pointer came or moved (screensaver) */
static const float MOUSE_SPEED = 1.25f;

/* A USB keyboard (libwiikeyboard): fixed keys, as Wii Remote buttons; and
 * what is typed, for text fields (typedChar) */
static int  s_kb = 0;                         /* 0 not tried, 1 driver up, -1 none */
static int  s_gamepad = 0;                    /* 0 not tried, 1 driver up, -1 none */
static char s_typed[32];
static int  s_typedLen = 0, s_typedPos = 0;

static u32 fromKey(u16 sym)
{
    switch (sym) {
    case KS_Up:     return WPAD_BUTTON_UP;
    case KS_Down:   return WPAD_BUTTON_DOWN;
    case KS_Left:   return WPAD_BUTTON_LEFT;
    case KS_Right:  return WPAD_BUTTON_RIGHT;
    case KS_Return:
    case KS_KP_Enter: return WPAD_BUTTON_A;
    case KS_Escape: return WPAD_BUTTON_B;
    case KS_f1:     return WPAD_BUTTON_1;
    case KS_f2:     return WPAD_BUTTON_2;
    case KS_Prior:  return WPAD_BUTTON_MINUS;
    case KS_Next:   return WPAD_BUTTON_PLUS;
    case KS_Home:   return WPAD_BUTTON_HOME;
    case KS_f3:     return Input::BTN_ZOOM;
    }
    return 0;
}

/* A GameCube stick pushed past half way: its D-pad direction */
static u32 padStickDir(int x, int y)
{
    const int T = 48;                             /* full tilt is about 100 */
    if (x * x + y * y <= T * T) return 0;
    const int ax = x < 0 ? -x : x;
    if (y > ax)  return WPAD_BUTTON_UP;
    if (-y > ax) return WPAD_BUTTON_DOWN;
    return x > 0 ? WPAD_BUTTON_RIGHT : WPAD_BUTTON_LEFT;
}

static u32 toStickBits(u32 dir)
{
    u32 out = 0;
    if (dir & WPAD_BUTTON_UP)    out |= Input::BTN_STICK_UP;
    if (dir & WPAD_BUTTON_DOWN)  out |= Input::BTN_STICK_DOWN;
    if (dir & WPAD_BUTTON_LEFT)  out |= Input::BTN_STICK_LEFT;
    if (dir & WPAD_BUTTON_RIGHT) out |= Input::BTN_STICK_RIGHT;
    return out;
}

static u32 toCStickBits(u32 dir)
{
    u32 out = 0;
    if (dir & WPAD_BUTTON_UP)    out |= Input::BTN_C_UP;
    if (dir & WPAD_BUTTON_DOWN)  out |= Input::BTN_C_DOWN;
    if (dir & WPAD_BUTTON_LEFT)  out |= Input::BTN_C_LEFT;
    if (dir & WPAD_BUTTON_RIGHT) out |= Input::BTN_C_RIGHT;
    return out;
}

static u32 stickDirs(u32 s)
{
    u32 out = 0;
    if (s & Input::BTN_STICK_UP)    out |= WPAD_BUTTON_UP;
    if (s & Input::BTN_STICK_DOWN)  out |= WPAD_BUTTON_DOWN;
    if (s & Input::BTN_STICK_LEFT)  out |= WPAD_BUTTON_LEFT;
    if (s & Input::BTN_STICK_RIGHT) out |= WPAD_BUTTON_RIGHT;
    return out;
}

/* A Classic Controller's left stick pushed past half way */
static u32 classicStickDir(int ch)
{
    WPADData* wd = WPAD_Data(ch);
    if (!wd || wd->exp.type != WPAD_EXP_CLASSIC) return 0;
    const joystick_t& js = wd->exp.classic.ljs;
    if (js.mag <= 0.5f) return 0;
    float a = js.ang;                             /* 0 up, 90 right */
    if (a >= 315 || a < 45) return WPAD_BUTTON_UP;
    if (a < 135)            return WPAD_BUTTON_RIGHT;
    if (a < 225)            return WPAD_BUTTON_DOWN;
    return WPAD_BUTTON_LEFT;
}

/* A Classic Controller's right stick (C-Stick) pushed past half way */
static u32 classicSubStickDir(int ch)
{
    WPADData* wd = WPAD_Data(ch);
    if (!wd || wd->exp.type != WPAD_EXP_CLASSIC) return 0;
    const joystick_t& js = wd->exp.classic.rjs;
    if (js.mag <= 0.5f) return 0;
    float a = js.ang;                             /* 0 up, 90 right */
    if (a >= 315 || a < 45) return WPAD_BUTTON_UP;
    if (a < 135)            return WPAD_BUTTON_RIGHT;
    if (a < 225)            return WPAD_BUTTON_DOWN;
    return WPAD_BUTTON_LEFT;
}

/* A Nunchuk's stick pushed past half way */
static u32 nunchukStickDir(int ch)
{
    WPADData* wd = WPAD_Data(ch);
    if (!wd || wd->exp.type != WPAD_EXP_NUNCHUK) return 0;
    const joystick_t& js = wd->exp.nunchuk.js;
    if (js.mag <= 0.5f) return 0;
    float a = js.ang;                             /* 0 up, 90 right */
    if (a >= 315 || a < 45) return WPAD_BUTTON_UP;
    if (a < 135)            return WPAD_BUTTON_RIGHT;
    if (a < 225)            return WPAD_BUTTON_DOWN;
    return WPAD_BUTTON_LEFT;
}

/* A GameCube controller's main analog stick */
static u32 gcMainStickDir(int port)
{
    if (!s_padUp[port]) return 0;
    return toStickBits(padStickDir(PAD_StickX(port), PAD_StickY(port)));
}

/* A GameCube controller's C-Stick (sub stick) */
static u32 gcSubStickDir(int port)
{
    if (!s_padUp[port]) return 0;
    return toCStickBits(padStickDir(PAD_SubStickX(port), PAD_SubStickY(port)));
}

/* A Wii U Pro Controller's left stick */
static u32 wupcMainStickDir(int ch)
{
    if (!WUPC_Data(ch)) return 0;
    return padStickDir(WUPC_lStickX(ch) / 10, WUPC_lStickY(ch) / 10);
}

/* A Wii U Pro Controller's right stick (C-Stick) */
static u32 wupcSubStickDir(int ch)
{
    if (!WUPC_Data(ch)) return 0;
    return padStickDir(WUPC_rStickX(ch) / 10, WUPC_rStickY(ch) / 10);
}

void Input::update() {
    static bool padInit = false;
    if (!padInit) { PAD_Init(); padInit = true; }
    WUPC_UpdateButtonStats();
    WPAD_ScanPads();
    /* a Wii Remote connecting (later than WiiFin, or back from sleep) is in
     * buttons-only mode: libogc sets the pointer's mode only on those there
     * when asked; so ask again for each one that comes */
    static bool remoteThere[WPAD_BALANCE_BOARD] = {};
    for (int ch = WPAD_CHAN_0; ch < WPAD_BALANCE_BOARD; ++ch) {
        u32 type;
        const bool there = WPAD_Probe(ch, &type) == WPAD_ERR_NONE;
        if (there && !remoteThere[ch]) {
            WPAD_SetDataFormat(ch, WPAD_FMT_BTNS_ACC_IR);
            WPAD_SetVRes(ch, 640, 480);
            SYS_Report("[Input] Wii Remote %d connected: pointer on\n", ch + 1);
        }
        remoteThere[ch] = there;
    }

    /* every GameCube port: pressed and held, Wii Remote bits */
    const u32 padsUp = PAD_ScanPads();
    u64 nowMs = ticks_to_millisecs(gettime());
    static u64 lastAnswer[PORTS] = {}, lastDropReport = 0;
    static u32 dropouts = 0;
    static bool padSeen[PORTS] = {};
    u32 gcDown = 0, gcHeld = 0;
    s_gcRawDown = 0;
    int gcPort = -1;                              /* the port pressed this frame */
    for (int port = 0; port < PORTS; ++port) {
        const bool up = (padsUp >> port) & 1;
        if (up) lastAnswer[port] = nowMs;
        else if (s_padUp[port]) ++dropouts;
        if (up && !padSeen[port]) SYS_Report("[Input] GameCube controller in port %d\n", port + 1);
        padSeen[port] = up || nowMs - lastAnswer[port] < 500;
        s_padUp[port] = up;
        /* no answer for 0.5 s: unplugged, nothing is held any more */
        u16 h = up ? PAD_ButtonsHeld(port) : (nowMs - lastAnswer[port] < 500 ? s_padHeld[port] : 0);
        u16 d = h & ~s_padHeld[port];
        s_padHeld[port] = h;
        gcDown |= fromGameCube(d);
        s_gcRawDown |= d;
        gcHeld |= fromGameCube(h);
        if (d && gcPort < 0) gcPort = port;
    }
    if (dropouts && nowMs - lastDropReport > 5000) {
        SYS_Report("[Input] GameCube controller: %u read(s) without answer\n", (unsigned)dropouts);
        dropouts = 0;
        lastDropReport = nowMs;
    }

    /* every Wii Remote (and its Classic Controller or Nunchuk) */
    u32 wiiDown = 0, wiiHeld = 0;
    bool plusDown = false;
    int remoteChan = -1, classicChan = -1;        /* pressed this frame */
    for (int ch = 0; ch < PORTS; ++ch) {
        if (!remoteThere[ch]) continue;
        WPADData* wd = WPAD_Data(ch);
        const bool classic = wd && wd->exp.type == WPAD_EXP_CLASSIC;
        const bool nunchuk = wd && wd->exp.type == WPAD_EXP_NUNCHUK;
        const u32 d = WPAD_ButtonsDown(ch);
        wiiDown |= fromWpad(d, classic, nunchuk);
        wiiHeld |= fromWpad(WPAD_ButtonsHeld(ch), classic, nunchuk);
        plusDown |= (d & WPAD_BUTTON_PLUS) || (classic && (d & WPAD_CLASSIC_BUTTON_PLUS))
                 || (nunchuk && (d & WPAD_NUNCHUK_BUTTON_Z));
        if (classic && (d & 0xFFFF0000u) && classicChan < 0) classicChan = ch;
        /* the Nunchuk is the Wii Remote's half: its hints, its pointer */
        if (((d & 0xFFFF) || (nunchuk && (d & 0xFFFF0000u))) && remoteChan < 0) remoteChan = ch;
    }

    /* every Wii U Pro Controller (Bluetooth) */
    for (int ch = 0; ch < PORTS; ++ch) {
        if (!WUPC_Data(ch)) continue;
        const u32 d = WUPC_ButtonsDown(ch);
        const u32 h = WUPC_ButtonsHeld(ch);
        wiiDown |= fromClassic(d);
        wiiHeld |= fromClassic(h);
        if (d & WPAD_CLASSIC_BUTTON_PLUS) plusDown = true;
        if (d && classicChan < 0) classicChan = ch;
    }

    /* the keyboard: every key since the last frame */
    if (s_kb == 0) {
        /* no callback: with one, libwiikeyboard empties its queue into a
         * stdin buffer that WiiFin never reads (#19) */
        s_kb = KEYBOARD_Init(nullptr) >= 0 ? 1 : -1;
        SYS_Report("[Input] USB keyboard driver %s\n", s_kb > 0 ? "up" : "unavailable");
    }
    u32 kbDown = 0, kbHeld = 0;
    bool kbUsed = false;
    s_typedLen = s_typedPos = 0;
    if (s_kb > 0) {
        static u32 keyBits[256] = {};             /* by keycode: what each held key is */
        keyboard_event ev;
        while (KEYBOARD_GetEvent(&ev) > 0) {
            if (ev.type == KEYBOARD_PRESSED) {
                kbUsed = true;
                const u32 b = fromKey(ev.symbol);
                if (b != keyBits[ev.keycode]) kbDown |= b;   /* not a repeat of a held key */
                keyBits[ev.keycode] = b;
                char c = 0;
                if (ev.symbol == KS_Return || ev.symbol == KS_KP_Enter)       c = '\n';
                else if (ev.symbol == KS_BackSpace)                           c = '\b';
                else if (ev.symbol == KS_Delete)                              c = 0x7F;
                else if (ev.symbol == KS_Left)                                c = 0x11;
                else if (ev.symbol == KS_Right)                               c = 0x12;
                else if (ev.symbol == KS_Home)                                c = 0x01;
                else if (ev.symbol == KS_End)                                 c = 0x05;
                else if (ev.symbol >= 32 && ev.symbol < 127)                  c = (char)ev.symbol;
                if (c && s_typedLen < (int)sizeof(s_typed)) s_typed[s_typedLen++] = c;
            } else if (ev.type == KEYBOARD_RELEASED) {
                keyBits[ev.keycode] = 0;
            } else if (ev.type == KEYBOARD_CONNECTED) {
                SYS_Report("[Input] USB keyboard connected\n");
            } else if (ev.type == KEYBOARD_DISCONNECTED) {
                memset(keyBits, 0, sizeof(keyBits));
            }
        }
        for (u32 b : keyBits) kbHeld |= b;
    }

    /* the mouse: what it did since the last frame */
    if (s_mouse == 0) {
        s_mouse = UsbMouse::init() ? 1 : -1;
        SYS_Report("[Input] USB mouse driver %s\n", s_mouse > 0 ? "up" : "unavailable");
    }
    u32 mouseDown = 0, mouseHeld = 0;
    s_mouseMoved = false;
    if (s_mouse > 0) {
        const UsbMouse::State m = UsbMouse::take();
        s_mouseX += m.dx * MOUSE_SPEED;
        s_mouseY += m.dy * MOUSE_SPEED;
        s_mouseX = s_mouseX < 0 ? 0 : s_mouseX > 639 ? 639 : s_mouseX;
        s_mouseY = s_mouseY < 0 ? 0 : s_mouseY > 479 ? 479 : s_mouseY;
        /* the mouse takes the pointer when it really moves (a few counts
         * since the last frame) or clicks or scrolls */
        static int drift = 0;
        drift += (m.dx < 0 ? -m.dx : m.dx) + (m.dy < 0 ? -m.dy : m.dy);
        if (drift >= 3 || m.pressed || m.wheel) {
            drift = 0;
            s_mouseMoved = true;
            if (!s_mouseOn) {
                s_mouseOn = true;
                SYS_Report("[Input] USB mouse: pointer\n");
            }
        } else if (!m.dx && !m.dy) {
            drift = 0;
        }
        if (s_mouseOn && !UsbMouse::connected()) {   /* unplugged: no pointer left on screen */
            s_mouseOn = false;
            SYS_Report("[Input] USB mouse gone\n");
        }
        if (m.pressed & 1) mouseDown |= WPAD_BUTTON_A;
        if (m.pressed & 2) mouseDown |= WPAD_BUTTON_B;
        if (m.wheel > 0)   mouseDown |= WPAD_BUTTON_UP;
        if (m.wheel < 0)   mouseDown |= WPAD_BUTTON_DOWN;
        if (m.held & 1)    mouseHeld |= WPAD_BUTTON_A;
        if (m.held & 2)    mouseHeld |= WPAD_BUTTON_B;
    }

    /* USB gamepad (Sony DualShock 4 / DualShock 3) */
    if (s_gamepad == 0) {
        s_gamepad = UsbGamepad::init() ? 1 : -1;
        SYS_Report("[Input] USB gamepad driver %s\n", s_gamepad > 0 ? "up" : "unavailable");
    }
    u32 usbPadDown = 0, usbPadHeld = 0;
    static u32 prevUsbMain = 0, prevUsbSub = 0;
    u32 usbMain = 0, usbSub = 0;
    if (s_gamepad > 0 && UsbGamepad::connected()) {
        const UsbGamepad::State gp = UsbGamepad::take();
        usbPadDown = gp.pressed;
        usbPadHeld = gp.held;
        usbMain = toStickBits(padStickDir(gp.stickX, gp.stickY));
        usbSub  = toCStickBits(padStickDir(gp.subX, gp.subY));
    }
    const u32 usbNew = (usbMain & ~prevUsbMain) | (usbSub & ~prevUsbSub);
    prevUsbMain = usbMain;
    prevUsbSub  = usbSub;
    if ((usbPadDown || usbNew) && classicChan < 0) classicChan = 0;
    if (usbPadDown & WPAD_CLASSIC_BUTTON_PLUS) plusDown = true;

    /* sticks: a new push counts as a press of that controller */
    static u32 prevGcMain[PORTS] = {};
    static u32 prevGcSub[PORTS] = {};
    static u32 prevClassicMain[PORTS] = {};
    static u32 prevClassicSub[PORTS] = {};
    static u32 prevNunchuk[PORTS] = {};
    static u32 prevWupcMain[PORTS] = {};
    static u32 prevWupcSub[PORTS] = {};
    u32 stick = 0, stickDown = 0;
    for (int i = 0; i < PORTS; ++i) {
        const u32 gm = gcMainStickDir(i);
        const u32 gs = gcSubStickDir(i);
        const u32 cm = toStickBits(classicStickDir(i));
        const u32 cs = toCStickBits(classicSubStickDir(i));
        const u32 n  = remoteThere[i] ? toStickBits(nunchukStickDir(i)) : 0;
        const u32 wm = toStickBits(wupcMainStickDir(i));
        const u32 ws = toCStickBits(wupcSubStickDir(i));

        const u32 gNew = (gm & ~prevGcMain[i]) | (gs & ~prevGcSub[i]);
        const u32 cNew = (cm & ~prevClassicMain[i]) | (cs & ~prevClassicSub[i]);
        const u32 nNew = (n & ~prevNunchuk[i]);
        const u32 wNew = (wm & ~prevWupcMain[i]) | (ws & ~prevWupcSub[i]);

        if (gNew && gcPort < 0)      gcPort = i;
        if (cNew && classicChan < 0) classicChan = i;
        if (nNew && remoteChan < 0)  remoteChan = i;
        if (wNew && classicChan < 0) classicChan = i;

        stickDown |= gNew | cNew | nNew | wNew;
        stick     |= gm | gs | cm | cs | n | wm | ws;

        prevGcMain[i]      = gm;
        prevGcSub[i]       = gs;
        prevClassicMain[i] = cm;
        prevClassicSub[i]  = cs;
        prevNunchuk[i]     = n;
        prevWupcMain[i]    = wm;
        prevWupcSub[i]     = ws;
    }

    stickDown |= usbNew;
    stick     |= usbMain | usbSub;

    /* any other controller's button or stick, or a key: the screen's own
     * highlight again (with the mouse's pointer up, A only acts on what it
     * points at) */
    if (remoteChan >= 0 || classicChan >= 0 || gcPort >= 0 || kbDown || usbPadDown || usbNew) s_mouseOn = false;

    u32 stickDirsDown = stickDirs(stickDown);
    u32 stickDirsHeld = stickDirs(stick);
    u32 down = wiiDown | gcDown | stickDown | stickDirsDown | mouseDown | kbDown | usbPadDown;
    if (kbDown & WPAD_BUTTON_PLUS) plusDown = true;   /* Page Down, as + */
    /* the hints follow the controller used last; the pointer, the Wii
     * Remote used last */
    if (gcPort >= 0)           Ui::setButtonStyle(Ui::ButtonStyle::GameCube);
    else if ((usbPadDown || usbNew) && UsbGamepad::isPlayStation())
        Ui::setButtonStyle(Ui::ButtonStyle::PlayStation);
    else if ((usbPadDown || usbNew) && UsbGamepad::isXbox())
        Ui::setButtonStyle(Ui::ButtonStyle::Xbox);
    else if (classicChan >= 0) Ui::setButtonStyle(Ui::ButtonStyle::Classic);
    else if (remoteChan >= 0)  Ui::setButtonStyle(Ui::ButtonStyle::WiiRemote);
    else if (kbUsed)           Ui::setButtonStyle(Ui::ButtonStyle::Keyboard);
    if (remoteChan >= 0)       s_pointerChan = remoteChan;
    else if (classicChan >= 0) s_pointerChan = classicChan;
    /* which controller sent what: for controller bug reports */
    if (down & ~DIRS)
        SYS_Report("[Input] pressed 0x%x (remote 0x%x ch %d, gamecube 0x%x port %d, stick 0x%x, mouse 0x%x, keys 0x%x, usbpad 0x%x)\n",
                   (unsigned)down, (unsigned)wiiDown, remoteChan >= 0 ? remoteChan + 1 : classicChan + 1,
                   (unsigned)gcDown, gcPort + 1, (unsigned)stickDown, (unsigned)mouseDown, (unsigned)kbDown, (unsigned)usbPadDown);
    u32 held = wiiHeld | gcHeld | stick | stickDirsHeld | mouseHeld | kbHeld | usbPadHeld;

    /* A held direction repeats: after 400 ms, then every 90 ms */
    static u64 heldSince[4], lastRepeat[4];
    static const u32 BIT[4] = { WPAD_BUTTON_UP, WPAD_BUTTON_DOWN, WPAD_BUTTON_LEFT, WPAD_BUTTON_RIGHT };
    u64 now = ticks_to_millisecs(gettime());
    u32 repeat = 0;
    for (int i = 0; i < 4; ++i) {
        if (!(held & BIT[i]))      { heldSince[i] = 0; continue; }
        if (down & BIT[i] || !heldSince[i]) { heldSince[i] = lastRepeat[i] = now; continue; }
        if (now - heldSince[i] >= 400 && now - lastRepeat[i] >= 90) {
            lastRepeat[i] = now;
            repeat |= BIT[i];
        }
    }

    /* + as an action: the Wii Remote's or the Classic's + button, or a
     * zoom button (the GameCube's Z), not the R triggers: they turn pages */
    actionDown = plusDown || (down & BTN_ZOOM);
    rawDownBits = down;
    heldBits    = held;
    buttonsDown = down | (repeat & DIRS);

    /* the screensaver: anything done keeps it away; the press that wakes
     * the screen does nothing else */
    const bool did = down || held || kbUsed || s_mouseMoved || s_pointerMoved;
    s_pointerMoved = false;
    if (Screensaver::input(did)) consume();
}

u32  Input::rawDown()        { return rawDownBits; }
u32  Input::held()           { return heldBits; }
bool Input::isHomePressed()  { return buttonsDown & WPAD_BUTTON_HOME; }
bool Input::isUpPressed()    { return buttonsDown & WPAD_BUTTON_UP; }
bool Input::isDownPressed()  { return buttonsDown & WPAD_BUTTON_DOWN; }
bool Input::isLeftPressed()  { return buttonsDown & WPAD_BUTTON_LEFT; }
bool Input::isRightPressed() { return buttonsDown & WPAD_BUTTON_RIGHT; }
bool Input::isAJustPressed() { return buttonsDown & WPAD_BUTTON_A; }
void Input::consume() { rawDownBits = buttonsDown = 0; actionDown = false; s_gcRawDown = 0; }
bool Input::isBPressed()     { return buttonsDown & WPAD_BUTTON_B; }
bool Input::isBackPressed()  { return buttonsDown & WPAD_BUTTON_B; }
bool Input::isLPressed()     { return buttonsDown & WPAD_BUTTON_MINUS; }
bool Input::isRPressed()     { return buttonsDown & WPAD_BUTTON_PLUS; }
bool Input::isActionPressed() { return actionDown; }
bool Input::is1Pressed()     { return buttonsDown & WPAD_BUTTON_1; }
bool Input::is2Pressed()     { return buttonsDown & WPAD_BUTTON_2; }
u16  Input::gcRawDown()      { return s_gcRawDown; }

int Input::pointerChan() { return s_pointerChan; }
bool Input::pointerIsMouse() { return s_mouseOn; }

float Input::pointerRoll()
{
    if (s_mouseOn) return 0.0f;
    orient_t o;
    WPAD_Orientation(s_pointerChan, &o);
    return o.roll;
}

int Input::typedChar()
{
    return s_typedPos < s_typedLen ? (unsigned char)s_typed[s_typedPos++] : 0;
}

/* KEYBOARD_Init queues USB requests and its thread keeps polling USB: a
 * request still in flight when IOS shuts down for power-off or exit
 * completes into freed memory (DSI in __usbv0_messageCB) */
void Input::shutdown()
{
    WUPC_Shutdown();
    if (s_kb > 0) KEYBOARD_Deinit();
    s_kb = -1;
    if (s_mouse > 0) UsbMouse::shutdown();
    s_mouse = -1;
    s_mouseOn = false;
    if (s_gamepad > 0) UsbGamepad::shutdown();
    s_gamepad = -1;
}

/* the pointer of the Wii Remote used last; another one pointing at the
 * screen when that one is not; the mouse's while it is the one moving */
void Input::readIR(ir_t& ir) {
    WPAD_IR(s_pointerChan, &ir);
    for (int ch = 0; !ir.valid && ch < PORTS; ++ch) {
        if (ch == s_pointerChan) continue;
        ir_t other;
        WPAD_IR(ch, &other);
        if (other.valid) { ir = other; s_pointerChan = ch; }
    }
    /* A Wii Remote pointer takes it back from the mouse when it moves away
     * from where it was when the mouse last moved (its anchor): a remote
     * lying still pointing at the screen, its pointer jittering, does not
     * fight a mouse, and a slow sweep of the remote still counts. */
    static bool  anchored = false;
    static float ax = 0, ay = 0;
    if (!s_mouseOn || !ir.valid || s_mouseMoved) {
        anchored = s_mouseOn && ir.valid;
        ax = ir.x; ay = ir.y;
    } else if (!anchored) {
        anchored = true;
        ax = ir.x; ay = ir.y;
    } else if (fabsf(ir.x - ax) + fabsf(ir.y - ay) > 24.0f) {
        s_mouseOn = false;
        anchored = false;
        SYS_Report("[Input] Wii Remote %d: pointer\n", s_pointerChan + 1);
    }
    if (s_mouseOn) {
        ir.valid = 1;
        ir.x = s_mouseX;
        ir.y = s_mouseY;
        ir.angle = 0;
    }
    /* the pointer brought onto the screen or moved (not its jitter): the
     * screensaver's activity */
    {
        static bool  was = false;
        static float px = 0, py = 0;
        if (ir.valid && (!was || fabsf(ir.x - px) + fabsf(ir.y - py) > 24.0f)) {
            if (was) s_pointerMoved = true;
            px = ir.x; py = ir.y;
        }
        if (ir.valid && !was) s_pointerMoved = true;
        was = ir.valid;
    }
    Ui::pointerToScreen(ir);
}
