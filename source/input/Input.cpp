#include "Input.h"
#include "../ui/Ui.h"
#include <wiiuse/wpad.h>
#include <ogc/pad.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/system.h>
#include <ogc/usbmouse.h>
#include <math.h>

static u32 rawDownBits = 0;   /* pressed this frame, no repeat */
static u32 heldBits    = 0;
static u32 buttonsDown = 0;   /* rawDownBits + repeated directions */
static bool actionDown = false;   /* isActionPressed */

static const u32 DIRS = WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT;

/* Classic Controller bits (upper half of the WPAD word) -> Wii Remote bits */
static u32 fromClassic(u32 c)
{
    static const struct { u32 from, to; } MAP[] = {
        { WPAD_CLASSIC_BUTTON_UP,    WPAD_BUTTON_UP },
        { WPAD_CLASSIC_BUTTON_DOWN,  WPAD_BUTTON_DOWN },
        { WPAD_CLASSIC_BUTTON_LEFT,  WPAD_BUTTON_LEFT },
        { WPAD_CLASSIC_BUTTON_RIGHT, WPAD_BUTTON_RIGHT },
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

/* The upper half holds the expansion's buttons: only the Classic
 * Controller's are wanted (a Nunchuk's C/Z use the same bits). */
static u32 fromWpad(u32 w, bool classic)
{
    return (w & 0xFFFF) | (classic ? fromClassic(w & 0xFFFF0000u) : 0);
}

static u32 fromGameCube(u16 p)
{
    static const struct { u16 from; u32 to; } MAP[] = {
        { PAD_BUTTON_UP,    WPAD_BUTTON_UP },
        { PAD_BUTTON_DOWN,  WPAD_BUTTON_DOWN },
        { PAD_BUTTON_LEFT,  WPAD_BUTTON_LEFT },
        { PAD_BUTTON_RIGHT, WPAD_BUTTON_RIGHT },
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

/* A USB mouse (boot protocol: most wired ones): the pointer while it is
 * the one moving, left click A, right click B, the wheel Up / Down.  A
 * Wii Remote pointing or pressed takes the pointer back. */
static int   s_mouse   = 0;                   /* 0 not tried, 1 driver up, -1 none */
static bool  s_mouseOn = false;               /* the mouse has the pointer */
static float s_mouseX  = 320, s_mouseY = 240; /* pointer space, 640 x 480 */
static u8    s_mouseBtn = 0;
static const float MOUSE_SPEED = 1.25f;

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

/* A GameCube controller's sticks (the C-Stick too) */
static u32 gcStickDir(int port)
{
    if (!s_padUp[port]) return 0;
    return padStickDir(PAD_StickX(port), PAD_StickY(port)) |
           padStickDir(PAD_SubStickX(port), PAD_SubStickY(port));
}

void Input::update() {
    static bool padInit = false;
    if (!padInit) { PAD_Init(); padInit = true; }
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
        gcHeld |= fromGameCube(h);
        if (d && gcPort < 0) gcPort = port;
    }
    if (dropouts && nowMs - lastDropReport > 5000) {
        SYS_Report("[Input] GameCube controller: %u read(s) without answer\n", (unsigned)dropouts);
        dropouts = 0;
        lastDropReport = nowMs;
    }

    /* every Wii Remote (and its Classic Controller) */
    u32 wiiDown = 0, wiiHeld = 0;
    bool plusDown = false;
    int remoteChan = -1, classicChan = -1;        /* pressed this frame */
    for (int ch = 0; ch < PORTS; ++ch) {
        if (!remoteThere[ch]) continue;
        WPADData* wd = WPAD_Data(ch);
        const bool classic = wd && wd->exp.type == WPAD_EXP_CLASSIC;
        const u32 d = WPAD_ButtonsDown(ch);
        wiiDown |= fromWpad(d, classic);
        wiiHeld |= fromWpad(WPAD_ButtonsHeld(ch), classic);
        plusDown |= (d & WPAD_BUTTON_PLUS) || (classic && (d & WPAD_CLASSIC_BUTTON_PLUS));
        if (classic && (d & 0xFFFF0000u) && classicChan < 0) classicChan = ch;
        if ((d & 0xFFFF) && remoteChan < 0) remoteChan = ch;
    }

    /* the mouse: every report since the last frame */
    if (s_mouse == 0) {
        s_mouse = MOUSE_Init() >= 0 ? 1 : -1;
        SYS_Report("[Input] USB mouse driver %s\n", s_mouse > 0 ? "up" : "unavailable");
    }
    u32 mouseDown = 0, mouseHeld = 0;
    if (s_mouse > 0) {
        mouse_event ev;
        u8 pressed = 0;
        int wheel = 0;
        bool moved = false;
        while (MOUSE_GetEvent(&ev) > 0) {
            if (ev.rx || ev.ry) moved = true;
            s_mouseX += ev.rx * MOUSE_SPEED;
            s_mouseY += ev.ry * MOUSE_SPEED;
            wheel    += ev.rz;
            pressed  |= ev.button & ~s_mouseBtn;     /* a click inside one frame counts */
            s_mouseBtn = ev.button;
        }
        s_mouseX = s_mouseX < 0 ? 0 : s_mouseX > 639 ? 639 : s_mouseX;
        s_mouseY = s_mouseY < 0 ? 0 : s_mouseY > 479 ? 479 : s_mouseY;
        if ((moved || pressed || wheel) && !s_mouseOn) {
            s_mouseOn = true;
            SYS_Report("[Input] USB mouse: pointer\n");
        }
        if (pressed & 1) mouseDown |= WPAD_BUTTON_A;
        if (pressed & 2) mouseDown |= WPAD_BUTTON_B;
        if (wheel > 0)   mouseDown |= WPAD_BUTTON_UP;
        if (wheel < 0)   mouseDown |= WPAD_BUTTON_DOWN;
        if (s_mouseBtn & 1) mouseHeld |= WPAD_BUTTON_A;
        if (s_mouseBtn & 2) mouseHeld |= WPAD_BUTTON_B;
    }
    if (remoteChan >= 0 || classicChan >= 0) s_mouseOn = false;   /* a Wii Remote's button */

    /* sticks: a new push counts as a press of that controller */
    static u32 prevStick[2][PORTS] = {};
    u32 stick = 0, stickDown = 0;
    for (int i = 0; i < PORTS; ++i) {
        const u32 g = gcStickDir(i), c = classicStickDir(i);
        if ((g & ~prevStick[0][i]) && gcPort < 0)      gcPort = i;
        if ((c & ~prevStick[1][i]) && classicChan < 0) classicChan = i;
        stickDown |= (g & ~prevStick[0][i]) | (c & ~prevStick[1][i]);
        stick |= g | c;
        prevStick[0][i] = g;
        prevStick[1][i] = c;
    }

    u32 down = wiiDown | gcDown | stickDown | mouseDown;
    /* the hints follow the controller used last; the pointer, the Wii
     * Remote used last */
    if (gcPort >= 0)           Ui::setButtonStyle(Ui::ButtonStyle::GameCube);
    else if (classicChan >= 0) Ui::setButtonStyle(Ui::ButtonStyle::Classic);
    else if (remoteChan >= 0)  Ui::setButtonStyle(Ui::ButtonStyle::WiiRemote);
    if (remoteChan >= 0)       s_pointerChan = remoteChan;
    else if (classicChan >= 0) s_pointerChan = classicChan;
    /* which controller sent what: for controller bug reports */
    if (down & ~DIRS)
        SYS_Report("[Input] pressed 0x%x (remote 0x%x ch %d, gamecube 0x%x port %d, stick 0x%x, mouse 0x%x)\n",
                   (unsigned)down, (unsigned)wiiDown, remoteChan >= 0 ? remoteChan + 1 : classicChan + 1,
                   (unsigned)gcDown, gcPort + 1, (unsigned)stickDown, (unsigned)mouseDown);
    u32 held = wiiHeld | gcHeld | stick | mouseHeld;

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
}

u32  Input::rawDown()        { return rawDownBits; }
u32  Input::held()           { return heldBits; }
bool Input::isHomePressed()  { return buttonsDown & WPAD_BUTTON_HOME; }
bool Input::isUpPressed()    { return buttonsDown & WPAD_BUTTON_UP; }
bool Input::isDownPressed()  { return buttonsDown & WPAD_BUTTON_DOWN; }
bool Input::isLeftPressed()  { return buttonsDown & WPAD_BUTTON_LEFT; }
bool Input::isRightPressed() { return buttonsDown & WPAD_BUTTON_RIGHT; }
bool Input::isAJustPressed() { return buttonsDown & WPAD_BUTTON_A; }
bool Input::isBPressed()     { return buttonsDown & WPAD_BUTTON_B; }
bool Input::isBackPressed()  { return buttonsDown & WPAD_BUTTON_B; }
bool Input::isLPressed()     { return buttonsDown & WPAD_BUTTON_MINUS; }
bool Input::isRPressed()     { return buttonsDown & WPAD_BUTTON_PLUS; }
bool Input::isActionPressed() { return actionDown; }
bool Input::is1Pressed()     { return buttonsDown & WPAD_BUTTON_1; }
bool Input::is2Pressed()     { return buttonsDown & WPAD_BUTTON_2; }

int Input::pointerChan() { return s_pointerChan; }

float Input::pointerRoll()
{
    if (s_mouseOn) return 0.0f;
    orient_t o;
    WPAD_Orientation(s_pointerChan, &o);
    return o.roll;
}

void Input::shutdown()
{
    if (s_mouse > 0) MOUSE_Deinit();
    s_mouse = -1;
    s_mouseOn = false;
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
    /* a Wii Remote pointer that moves takes it back from the mouse */
    static float lastX = -1, lastY = -1;
    if (ir.valid) {
        if (s_mouseOn && lastX >= 0 && fabsf(ir.x - lastX) + fabsf(ir.y - lastY) > 12.0f) s_mouseOn = false;
        lastX = ir.x; lastY = ir.y;
    } else {
        lastX = lastY = -1;
    }
    if (s_mouseOn) {
        ir.valid = 1;
        ir.x = s_mouseX;
        ir.y = s_mouseY;
        ir.angle = 0;
    }
    Ui::pointerToScreen(ir);
}
