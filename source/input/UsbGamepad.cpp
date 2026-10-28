#include "UsbGamepad.h"
#include "Input.h"
#include <gccore.h>
#include <ogc/usb.h>
#include <ogc/ipc.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/semaphore.h>
#include <ogc/system.h>
#include <wiiuse/wpad.h>
#include <string.h>
#include <unistd.h>

namespace {

const int MAX_DEVICES = 16;
const int MAX_REPORT  = 64;

enum class PadType {
    None,
    DualShock4,
    DualShock3,
    GenericHID
};

struct Candidate {
    usb_device_entry dev;
    u8               iface;
    u8               ep;
    u16              len;
    PadType          type;
};

struct GamepadDevice {
    bool    connected = false;
    s32     fd = -1;
    u8      ep = 0;
    u16     len = 64;
    PadType type = PadType::None;
    u16     vid = 0;
    u16     pid = 0;
};

s32           s_heap = -1;
u8*           s_buf = nullptr;       /* 32-aligned, IOS heap for interrupt reads */
u8*           s_cmdBuf = nullptr;    /* 32-aligned, IOS heap for control transfers */
GamepadDevice s_dev;
lwp_t         s_thread = LWP_THREAD_NULL;
sem_t         s_readSema = LWP_SEM_NULL;
sem_t         s_waitSema = LWP_SEM_NULL;
mutex_t       s_lock = LWP_MUTEX_NULL;
volatile bool s_quit = false;
volatile s32  s_result = 0;
u8 s_stack[16 * 1024] __attribute__((aligned(32)));

/* Current controller state (protected by s_lock) */
u32 s_held = 0;
u32 s_pressedAccum = 0;
s8  s_stickX = 0, s_stickY = 0;
s8  s_subX = 0,   s_subY = 0;

s32 readCb(s32 result, void*)
{
    s_result = result;
    LWP_SemPost(s_readSema);
    return 0;
}

s32 removedCb(s32, void*)
{
    LWP_SemPost(s_readSema);
    LWP_SemPost(s_waitSema);
    return 0;
}

s32 changeCb(s32, void*)
{
    LWP_SemPost(s_waitSema);
    return 0;
}

PadType identifyPad(u16 vid, u16 pid)
{
    if (vid == 0x054C) {
        /* Sony Interactive Entertainment */
        if (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0) return PadType::DualShock4;
        if (pid == 0x0268) return PadType::DualShock3;
    }
    return PadType::GenericHID;
}

const usb_endpointdesc* inEndpoint(const usb_interfacedesc* id)
{
    for (int e = 0; e < id->bNumEndpoints; ++e) {
        const usb_endpointdesc* ed = &id->endpoints[e];
        if ((ed->bmAttributes & 3) == USB_ENDPOINT_INTERRUPT && (ed->bEndpointAddress & USB_ENDPOINT_IN))
            return ed;
    }
    return nullptr;
}

int scanCandidates(Candidate* c, int max)
{
    u8 count = 0;
    usb_device_entry list[MAX_DEVICES];
    memset(list, 0, sizeof(list));
    if (USB_GetDeviceList(list, MAX_DEVICES, USB_CLASS_HID, &count) < 0) return 0;

    int nc = 0;
    for (int i = 0; i < count && nc < max; ++i) {
        const usb_device_entry& e = list[i];
        if (!e.vid || !e.pid) continue;

        usb_devdesc dd;
        memset(&dd, 0, sizeof(dd));
        s32 got = e.device_id >= 0x20 || e.device_id < -1 ? USB_GetDescriptors(e.device_id, &dd) : -1;
        if (got < 0) {
            s32 fd = -1;
            if (USB_OpenDevice(e.device_id, e.vid, e.pid, &fd) >= 0) {
                got = USB_GetDescriptors(fd, &dd);
                USB_CloseDevice(&fd);
            }
        }
        if (got < 0 || dd.bNumConfigurations < 1) continue;

        for (int conf = 0; conf < dd.bNumConfigurations && nc < max; ++conf) {
            const usb_configurationdesc& cd = dd.configurations[conf];
            for (int f = 0; f < cd.bNumInterfaces && cd.interfaces && nc < max; ++f) {
                const usb_interfacedesc* id = &cd.interfaces[f];
                const usb_endpointdesc* ed = inEndpoint(id);
                if (id->bInterfaceClass != USB_CLASS_HID || !ed) continue;
                /* Skip mice and keyboards */
                if (id->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD || id->bInterfaceProtocol == USB_PROTOCOL_MOUSE)
                    continue;

                PadType pt = identifyPad(e.vid, e.pid);
                c[nc++] = Candidate{ e, (u8)id->bInterfaceNumber, (u8)ed->bEndpointAddress, (u16)ed->wMaxPacketSize, pt };
            }
        }
        USB_FreeDescriptors(&dd);
    }
    return nc;
}

bool openCandidate(const Candidate& c)
{
    s32 fd = -1;
    if (USB_OpenDevice(c.dev.device_id, c.dev.vid, c.dev.pid, &fd) < 0) return false;

    if (USB_DeviceRemovalNotifyAsync(fd, removedCb, nullptr) < 0) {
        USB_CloseDevice(&fd);
        return false;
    }

    if (c.type == PadType::DualShock3) {
        /* DualShock 3 operational report activation packet */
        if (s_cmdBuf) {
            const u8 enableCmd[4] = { 0x42, 0x0c, 0x00, 0x00 };
            memcpy(s_cmdBuf, enableCmd, sizeof(enableCmd));
            s32 r = USB_WriteCtrlMsg(fd,
                USB_REQTYPE_INTERFACE_SET,  /* 0x21 */
                USB_REQ_SETREPORT,          /* 0x09 */
                (3 << 8) | 0xf4,            /* Feature Report 0xF4 */
                c.iface,
                sizeof(enableCmd),
                s_cmdBuf);
            SYS_Report("[Gamepad] DualShock 3 activation: %d\n", (int)r);
        }
    }

    s_dev.fd        = fd;
    s_dev.ep        = c.ep;
    s_dev.len       = c.len > 0 && c.len <= MAX_REPORT ? c.len : (c.type == PadType::DualShock3 ? 49 : 64);
    s_dev.type      = c.type;
    s_dev.vid       = c.dev.vid;
    s_dev.pid       = c.dev.pid;
    s_dev.connected = true;

    SYS_Report("[Gamepad] Connected %s (VID 0x%04x PID 0x%04x, len %d)\n",
               c.type == PadType::DualShock4 ? "DualShock 4" : c.type == PadType::DualShock3 ? "DualShock 3" : "Generic USB Gamepad",
               c.dev.vid, c.dev.pid, (int)s_dev.len);
    return true;
}

void decodeDualShock4(const u8* buf, int len)
{
    if (len < 8) return;
    /* buf[0] is Report ID (usually 0x01) */
    const int offset = (buf[0] == 0x01 && len >= 9) ? 1 : 0;

    const int rawLx = buf[offset + 0];
    const int rawLy = buf[offset + 1];
    const int rawRx = buf[offset + 2];
    const int rawRy = buf[offset + 3];

    /* Convert to -128..127 with positive = up */
    const s8 sx = (s8)(rawLx - 128);
    const s8 sy = (s8)(128 - rawLy);
    const s8 rx = (s8)(rawRx - 128);
    const s8 ry = (s8)(128 - rawRy);

    u32 held = 0;

    /* D-pad hat switch (bits 0..3) */
    const u8 hat = buf[offset + 4] & 0x0F;
    switch (hat) {
    case 0: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP); break;
    case 1: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
    case 2: held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
    case 3: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
    case 4: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN); break;
    case 5: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
    case 6: held |= (WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
    case 7: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
    default: break;
    }

    /* Face buttons */
    const u8 b5 = buf[offset + 4];
    if (b5 & (1 << 4)) held |= WPAD_BUTTON_1;        /* Square -> Y */
    if (b5 & (1 << 5)) held |= WPAD_BUTTON_A;        /* Cross -> A */
    if (b5 & (1 << 6)) held |= WPAD_BUTTON_B;        /* Circle -> B */
    if (b5 & (1 << 7)) held |= WPAD_BUTTON_2;        /* Triangle -> X */

    /* Shoulder, Options, Stick clicks */
    const u8 b6 = buf[offset + 5];
    if (b6 & (1 << 0)) held |= WPAD_BUTTON_MINUS;    /* L1 */
    if (b6 & (1 << 1)) held |= WPAD_BUTTON_PLUS;     /* R1 */
    if (b6 & (1 << 2)) held |= Input::BTN_ZOOM;      /* L2 */
    if (b6 & (1 << 3)) held |= Input::BTN_ZOOM;      /* R2 */
    if (b6 & (1 << 4)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Share */
    if (b6 & (1 << 5)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Options */
    if (b6 & (1 << 6)) held |= Input::BTN_ZOOM;      /* L3 */
    if (b6 & (1 << 7)) held |= Input::BTN_ZOOM;      /* R3 */

    /* PS Home button & Touchpad click */
    const u8 b7 = buf[offset + 6];
    if (b7 & (1 << 0)) held |= WPAD_BUTTON_HOME;     /* PS Button */
    if (b7 & (1 << 1)) held |= Input::BTN_ZOOM;      /* Touchpad click */

    LWP_MutexLock(s_lock);
    const u32 pressed = held & ~s_held;
    s_pressedAccum |= pressed;
    s_held   = held;
    s_stickX = sx;
    s_stickY = sy;
    s_subX   = rx;
    s_subY   = ry;
    LWP_MutexUnlock(s_lock);
}

void decodeDualShock3(const u8* buf, int len)
{
    if (len < 10) return;

    /* D-pad & standard buttons in buf[2] */
    u32 held = 0;
    const u8 b2 = buf[2];
    if (b2 & (1 << 0)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Select */
    if (b2 & (1 << 1)) held |= Input::BTN_ZOOM;      /* L3 */
    if (b2 & (1 << 2)) held |= Input::BTN_ZOOM;      /* R3 */
    if (b2 & (1 << 3)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Start */
    if (b2 & (1 << 4)) held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP);
    if (b2 & (1 << 5)) held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT);
    if (b2 & (1 << 6)) held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN);
    if (b2 & (1 << 7)) held |= (WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT);

    /* Triggers & face buttons in buf[3] */
    const u8 b3 = buf[3];
    if (b3 & (1 << 0)) held |= Input::BTN_ZOOM;      /* L2 */
    if (b3 & (1 << 1)) held |= Input::BTN_ZOOM;      /* R2 */
    if (b3 & (1 << 2)) held |= WPAD_BUTTON_MINUS;    /* L1 */
    if (b3 & (1 << 3)) held |= WPAD_BUTTON_PLUS;     /* R1 */
    if (b3 & (1 << 4)) held |= WPAD_BUTTON_2;        /* Triangle -> X */
    if (b3 & (1 << 5)) held |= WPAD_BUTTON_B;        /* Circle -> B */
    if (b3 & (1 << 6)) held |= WPAD_BUTTON_A;        /* Cross -> A */
    if (b3 & (1 << 7)) held |= WPAD_BUTTON_1;        /* Square -> Y */

    /* PS Button in buf[4] */
    const u8 b4 = buf[4];
    if (b4 & (1 << 0)) held |= WPAD_BUTTON_HOME;     /* PS Button */

    /* Analog sticks */
    const int rawLx = buf[6];
    const int rawLy = buf[7];
    const int rawRx = buf[8];
    const int rawRy = buf[9];

    const s8 sx = (s8)(rawLx - 128);
    const s8 sy = (s8)(128 - rawLy);
    const s8 rx = (s8)(rawRx - 128);
    const s8 ry = (s8)(128 - rawRy);

    LWP_MutexLock(s_lock);
    const u32 pressed = held & ~s_held;
    s_pressedAccum |= pressed;
    s_held   = held;
    s_stickX = sx;
    s_stickY = sy;
    s_subX   = rx;
    s_subY   = ry;
    LWP_MutexUnlock(s_lock);
}

void decodeGenericHID(const u8* buf, int len)
{
    if (len < 4) return;

    /* Analog sticks: X/Y on buf[0]/buf[1], Z/Rz on buf[2]/buf[3] */
    const s8 sx = (s8)((int)buf[0] - 128);
    const s8 sy = (s8)(128 - (int)buf[1]);
    s8 rx = 0, ry = 0;
    if (len >= 4) {
        rx = (s8)((int)buf[2] - 128);
        ry = (s8)(128 - (int)buf[3]);
    }

    u32 held = 0;

    const bool isDragonRiseLike = (s_dev.vid == 0x0079 || s_dev.vid == 0x2563 ||
                                   s_dev.vid == 0x0810 || s_dev.vid == 0x11ff ||
                                   s_dev.vid == 0x0e8f || (len >= 5 && (buf[4] & 0x0F) >= 8));

    if (isDragonRiseLike && len >= 5) {
        /* Hat switch in lower 4 bits of buf[4] */
        const u8 hat = buf[4] & 0x0F;
        if (hat <= 7) {
            switch (hat) {
            case 0: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP); break;
            case 1: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
            case 2: held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
            case 3: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
            case 4: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN); break;
            case 5: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
            case 6: held |= (WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
            case 7: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
            default: break;
            }
        }

        /* Face buttons in upper 4 bits of buf[4] */
        const u8 b4 = (buf[4] >> 4) & 0x0F;
        if (b4 & (1 << 0)) held |= WPAD_BUTTON_2;      /* Button 1 (Triangle) */
        if (b4 & (1 << 1)) held |= WPAD_BUTTON_B;      /* Button 2 (Circle) */
        if (b4 & (1 << 2)) held |= WPAD_BUTTON_A;      /* Button 3 (Cross) */
        if (b4 & (1 << 3)) held |= WPAD_BUTTON_1;      /* Button 4 (Square) */

        /* Shoulders & extra buttons in buf[5] */
        if (len >= 6) {
            const u8 b5 = buf[5];
            if (b5 & (1 << 0)) held |= WPAD_BUTTON_MINUS; /* Button 5 (L1) */
            if (b5 & (1 << 1)) held |= WPAD_BUTTON_PLUS;  /* Button 6 (R1) */
            if (b5 & (1 << 2)) held |= Input::BTN_ZOOM;   /* Button 7 (L2) */
            if (b5 & (1 << 3)) held |= Input::BTN_ZOOM;   /* Button 8 (R2) */
            if (b5 & (1 << 4)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Button 9 (Select) */
            if (b5 & (1 << 5)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Button 10 (Start) */
            if (b5 & (1 << 6)) held |= Input::BTN_ZOOM;   /* Button 11 (L3) */
            if (b5 & (1 << 7)) held |= Input::BTN_ZOOM;   /* Button 12 (R3) */
        }
        if (len >= 7) {
            const u8 b6 = buf[6];
            if (b6 & (1 << 0)) held |= WPAD_BUTTON_HOME;  /* Button 13 (Home / Mode) */
        }
    } else {
        /* Standard DirectInput gamepad layout */
        if (len >= 5) {
            const u8 b4 = buf[4];
            if (b4 & (1 << 0)) held |= WPAD_BUTTON_A;      /* Button 1 (A) */
            if (b4 & (1 << 1)) held |= WPAD_BUTTON_B;      /* Button 2 (B) */
            if (b4 & (1 << 2)) held |= WPAD_BUTTON_1;      /* Button 3 (X) */
            if (b4 & (1 << 3)) held |= WPAD_BUTTON_2;      /* Button 4 (Y) */
            if (b4 & (1 << 4)) held |= WPAD_BUTTON_MINUS;  /* Button 5 (L1) */
            if (b4 & (1 << 5)) held |= WPAD_BUTTON_PLUS;   /* Button 6 (R1) */
            if (b4 & (1 << 6)) held |= Input::BTN_ZOOM;    /* Button 7 (L2) */
            if (b4 & (1 << 7)) held |= Input::BTN_ZOOM;    /* Button 8 (R2) */
        }
        if (len >= 6) {
            const u8 b5 = buf[5];
            if (b5 & (1 << 0)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Button 9 (Select) */
            if (b5 & (1 << 1)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Button 10 (Start) */
            if (b5 & (1 << 2)) held |= Input::BTN_ZOOM;   /* Button 11 (L3) */
            if (b5 & (1 << 3)) held |= Input::BTN_ZOOM;   /* Button 12 (R3) */
            if (b5 & (1 << 4)) held |= WPAD_BUTTON_HOME;  /* Button 13 (Home/Mode) */
        }
        if (len >= 7) {
            const u8 hat = buf[6] & 0x0F;
            if (hat <= 7) {
                switch (hat) {
                case 0: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP); break;
                case 1: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
                case 2: held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
                case 3: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT); break;
                case 4: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN); break;
                case 5: held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
                case 6: held |= (WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
                case 7: held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT); break;
                default: break;
                }
            }
        }
    }

    LWP_MutexLock(s_lock);
    const u32 pressed = held & ~s_held;
    s_pressedAccum |= pressed;
    s_held   = held;
    s_stickX = sx;
    s_stickY = sy;
    s_subX   = rx;
    s_subY   = ry;
    LWP_MutexUnlock(s_lock);
}

void* threadMain(void*)
{
    while (!s_quit) {
        if (!s_dev.connected) {
            Candidate cand[MAX_DEVICES];
            int n = scanCandidates(cand, MAX_DEVICES);
            for (int i = 0; i < n; ++i) {
                if (openCandidate(cand[i])) break;
            }
            if (!s_dev.connected) {
                USB_DeviceChangeNotifyAsync(USB_CLASS_HID, changeCb, nullptr);
                LWP_SemWait(s_waitSema);
                continue;
            }
        }

        if (s_quit) break;

        /* Submit interrupt read request */
        if (USB_ReadIntrMsgAsync(s_dev.fd, s_dev.ep, s_dev.len, s_buf, readCb, nullptr) < 0) {
            USB_CloseDevice(&s_dev.fd);
            s_dev.connected = false;
            LWP_MutexLock(s_lock);
            s_held = 0;
            s_stickX = s_stickY = s_subX = s_subY = 0;
            LWP_MutexUnlock(s_lock);
            continue;
        }

        LWP_SemWait(s_readSema);
        if (s_quit) break;

        if (s_result > 0) {
            if (s_dev.type == PadType::DualShock4) {
                decodeDualShock4(s_buf, s_result);
            } else if (s_dev.type == PadType::DualShock3) {
                decodeDualShock3(s_buf, s_result);
            } else if (s_dev.type == PadType::GenericHID) {
                decodeGenericHID(s_buf, s_result);
            }
        } else if (s_result < 0) {
            SYS_Report("[Gamepad] Controller disconnected (%d)\n", (int)s_result);
            USB_CloseDevice(&s_dev.fd);
            s_dev.connected = false;
            LWP_MutexLock(s_lock);
            s_held = 0;
            s_stickX = s_stickY = s_subX = s_subY = 0;
            LWP_MutexUnlock(s_lock);
        }
    }

    if (s_dev.fd >= 0) {
        USB_CloseDevice(&s_dev.fd);
        s_dev.connected = false;
    }
    return nullptr;
}

} // namespace

bool UsbGamepad::init()
{
    if (s_thread != LWP_THREAD_NULL) return true;
    if (USB_Initialize() != IPC_OK) return false;
    if (s_heap < 0) s_heap = iosCreateHeap(2048);
    if (s_heap < 0) return false;
    if (!s_buf)    s_buf    = (u8*)iosAlloc(s_heap, MAX_REPORT);
    if (!s_cmdBuf) s_cmdBuf = (u8*)iosAlloc(s_heap, 32);
    if (!s_buf || !s_cmdBuf) return false;

    if (s_readSema == LWP_SEM_NULL) LWP_SemInit(&s_readSema, 0, 1);
    if (s_waitSema == LWP_SEM_NULL) LWP_SemInit(&s_waitSema, 0, 1);
    if (s_lock == LWP_MUTEX_NULL)   LWP_MutexInit(&s_lock, false);

    s_quit = false;
    if (LWP_CreateThread(&s_thread, threadMain, nullptr, s_stack, sizeof(s_stack), 65) < 0) {
        s_thread = LWP_THREAD_NULL;
        return false;
    }
    return true;
}

void UsbGamepad::shutdown()
{
    if (s_thread == LWP_THREAD_NULL) return;
    s_quit = true;
    LWP_SemPost(s_readSema);
    LWP_SemPost(s_waitSema);
    LWP_JoinThread(s_thread, nullptr);
    s_thread = LWP_THREAD_NULL;
}

UsbGamepad::State UsbGamepad::take()
{
    State st;
    LWP_MutexLock(s_lock);
    st.pressed   = s_pressedAccum;
    st.held      = s_held;
    st.stickX    = s_stickX;
    st.stickY    = s_stickY;
    st.subX      = s_subX;
    st.subY      = s_subY;
    st.connected = s_dev.connected;
    s_pressedAccum = 0;
    LWP_MutexUnlock(s_lock);
    return st;
}

bool UsbGamepad::connected()
{
    return s_dev.connected;
}

bool UsbGamepad::isPlayStation()
{
    return s_dev.connected && (s_dev.type == PadType::DualShock4 || s_dev.type == PadType::DualShock3);
}
