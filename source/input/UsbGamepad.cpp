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
    XboxOne,
    Xbox360,
    SwitchPro,
    GenericHID
};

struct Candidate {
    usb_device_entry dev;
    u8               iface;
    u8               ep;
    u8               epOut;
    u16              len;
    PadType          type;
};

struct GamepadDevice {
    bool    connected = false;
    s32     fd = -1;
    u8      ep = 0;
    u8      epOut = 0;
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
static bool   s_xboxGuideHeld = false;
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

s32 writeDummyCb(s32, void*)
{
    return 0;
}

PadType identifyPad(u16 vid, u16 pid, u8 ifaceClass = 0, u8 ifaceSubClass = 0, u8 ifaceProto = 0)
{
    /* Check interface class/subclass/protocol for Xbox vendor-specific controllers */
    if (ifaceClass == 0xFF) {
        if (ifaceSubClass == 0x47 || ifaceProto == 0xD0) {
            return PadType::XboxOne;
        }
        if (ifaceSubClass == 0x5D || ifaceProto == 0x01) {
            return PadType::Xbox360;
        }
    }

    /* Nintendo Switch controllers */
    if (vid == 0x057E) {
        if (pid == 0x2009 || pid == 0x2006 || pid == 0x2007 || pid == 0x200E ||
            pid == 0x2017 || pid == 0x2019 || pid == 0x201E) {
            return PadType::SwitchPro;
        }
    }

    /* Microsoft Official Xbox One & Series X|S */
    if (vid == 0x045E) {
        if (pid == 0x02D1 || pid == 0x02DD || pid == 0x02E3 || pid == 0x02EA ||
            pid == 0x02FD || pid == 0x0B00 || pid == 0x0B05 || pid == 0x0B0A ||
            pid == 0x0B12 || pid == 0x0B13 || pid == 0x0B20 || pid == 0x0B22) {
            return PadType::XboxOne;
        }
        if (pid == 0x028E || pid == 0x028F || pid == 0x0719) {
            return PadType::Xbox360;
        }
    }

    /* Third-party Xbox One / Series X|S controllers */
    if (vid == 0x24C6 /* PowerA */ || vid == 0x0E6F /* PDP */ || vid == 0x2E24 /* Hyperkin */ ||
        vid == 0x10F5 /* Turtle Beach */) {
        return PadType::XboxOne;
    }
    if (vid == 0x0F0D && (pid == 0x0063 || pid == 0x0067 || pid == 0x00C1 || pid == 0x00DC)) {
        return PadType::XboxOne; /* Hori Xbox One pads */
    }
    if (vid == 0x1532 && (pid == 0x0A00 || pid == 0x0A03 || pid == 0x0A14 || pid == 0x0A15 || pid == 0x0A29)) {
        return PadType::XboxOne; /* Razer Wolverine / Wildcat */
    }

    /* Third-party Xbox 360 controllers */
    if (vid == 0x11C9 || vid == 0x1689 || vid == 0x1BAD) {
        return PadType::Xbox360;
    }

    /* Sony Interactive Entertainment */
    if (vid == 0x054C) {
        if (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0 || pid == 0x0CE6 || pid == 0x0DF2)
            return PadType::DualShock4;
        if (pid == 0x0268) return PadType::DualShock3;
    }
    /* Third-party / Clone PlayStation controllers */
    if (vid == 0x0F0D || vid == 0x146B || vid == 0x0C12 || vid == 0x2C22 || vid == 0x0738 || vid == 0x1532) {
        return PadType::DualShock4;
    }
    if (vid == 0x2563 || vid == 0x0888) {
        if (pid == 0x0575 || pid == 0x0268) return PadType::DualShock3;
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

const usb_endpointdesc* outEndpoint(const usb_interfacedesc* id)
{
    for (int e = 0; e < id->bNumEndpoints; ++e) {
        const usb_endpointdesc* ed = &id->endpoints[e];
        if ((ed->bmAttributes & 3) == USB_ENDPOINT_INTERRUPT && !(ed->bEndpointAddress & USB_ENDPOINT_IN))
            return ed;
    }
    return nullptr;
}

int scanCandidates(Candidate* c, int max)
{
    u8 count = 0;
    usb_device_entry list[MAX_DEVICES];
    memset(list, 0, sizeof(list));

    /* First query class 0 (returns all attached devices on modern IOS / ven_host + hid_host) */
    if (USB_GetDeviceList(list, MAX_DEVICES, 0, &count) < 0 || count == 0) {
        u8 countHid = 0, countVendor = 0;
        if (USB_GetDeviceList(list, MAX_DEVICES, USB_CLASS_HID, &countHid) < 0) countHid = 0;
        if (countHid < MAX_DEVICES) {
            if (USB_GetDeviceList(list + countHid, MAX_DEVICES - countHid, 0xFF, &countVendor) < 0) countVendor = 0;
        }
        count = countHid + countVendor;
    }
    if (count == 0) return 0;

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
        if (got < 0 || dd.bNumConfigurations < 1) {
            /* Fallback detection by VID/PID if descriptor querying failed on vendor endpoints */
            PadType pt = identifyPad(e.vid, e.pid);
            if (pt == PadType::XboxOne) {
                c[nc++] = Candidate{ e, 0, 0x81, 0x01, 64, pt };
            } else if (pt == PadType::Xbox360) {
                c[nc++] = Candidate{ e, 0, 0x81, 0x02, 32, pt };
            }
            continue;
        }

        for (int conf = 0; conf < dd.bNumConfigurations && nc < max; ++conf) {
            const usb_configurationdesc& cd = dd.configurations[conf];
            for (int f = 0; f < cd.bNumInterfaces && cd.interfaces && nc < max; ++f) {
                const usb_interfacedesc* id = &cd.interfaces[f];
                const usb_endpointdesc* edIn = inEndpoint(id);
                if (!edIn) continue;

                /* Skip mice and keyboards */
                if (id->bInterfaceClass == USB_CLASS_HID &&
                    (id->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD || id->bInterfaceProtocol == USB_PROTOCOL_MOUSE))
                    continue;

                PadType pt = identifyPad(e.vid, e.pid, id->bInterfaceClass, id->bInterfaceSubClass, id->bInterfaceProtocol);
                const bool isXboxType = (pt == PadType::XboxOne || pt == PadType::Xbox360);
                const bool isHid = (id->bInterfaceClass == USB_CLASS_HID);
                const bool isXboxIntf = (id->bInterfaceClass == 0xFF &&
                                        (id->bInterfaceSubClass == 0x47 || id->bInterfaceSubClass == 0x5D ||
                                         id->bInterfaceProtocol == 0xD0 || id->bInterfaceProtocol == 0x01 ||
                                         id->bInterfaceNumber == 0));

                if (!isHid && !isXboxType && !isXboxIntf) continue;
                if ((isXboxType || isXboxIntf) && id->bInterfaceNumber != 0) continue; /* Xbox controller data is on interface 0 */

                const usb_endpointdesc* edOut = outEndpoint(id);
                const u8 epOutAddr = edOut ? (u8)edOut->bEndpointAddress : 0;

                c[nc++] = Candidate{ e, (u8)id->bInterfaceNumber, (u8)edIn->bEndpointAddress, epOutAddr, (u16)edIn->wMaxPacketSize, pt };
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

    /* Check configuration and activate configuration 1 if unconfigured */
    u8 config = 0;
    if (USB_GetConfiguration(fd, &config) < 0 || config == 0) {
        USB_SetConfiguration(fd, 1);
    }

    /* Register device removal notification (do not fail if unsupported on vendor nodes) */
    USB_DeviceRemovalNotifyAsync(fd, removedCb, nullptr);

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
    } else if (c.type == PadType::XboxOne) {
        if (s_cmdBuf && c.epOut) {
            /* 1. GIP Power-On packet */
            const u8 xboxone_init[5] = { 0x05, 0x20, 0x00, 0x01, 0x00 };
            memcpy(s_cmdBuf, xboxone_init, sizeof(xboxone_init));
            USB_WriteIntrMsgAsync(fd, c.epOut, sizeof(xboxone_init), s_cmdBuf, writeDummyCb, nullptr);
            usleep(10000);

            /* 2. GIP S / Series initialization packet (required for Xbox One S, Series X|S, Elite 2) */
            const u8 xboxone_s_init[5] = { 0x05, 0x20, 0x01, 0x0f, 0x06 };
            memcpy(s_cmdBuf, xboxone_s_init, sizeof(xboxone_s_init));
            USB_WriteIntrMsgAsync(fd, c.epOut, sizeof(xboxone_s_init), s_cmdBuf, writeDummyCb, nullptr);

            SYS_Report("[Gamepad] Xbox One / Series init sent (ep 0x%02x)\n", c.epOut);
        }
    } else if (c.type == PadType::SwitchPro) {
        if (s_cmdBuf && c.epOut) {
            /* Switch Pro USB mode handshake:
             * Send handshake (0x80, 0x02) and force USB mode (0x80, 0x04).
             * Never send 0x80, 0x03 (High Speed 480Mbps) because Wii OH0 is Full Speed (12Mbps) only! */
            s_cmdBuf[0] = 0x80; s_cmdBuf[1] = 0x02;
            USB_WriteIntrMsgAsync(fd, c.epOut, 2, s_cmdBuf, writeDummyCb, nullptr);
            usleep(10000);
            s_cmdBuf[0] = 0x80; s_cmdBuf[1] = 0x04;
            USB_WriteIntrMsgAsync(fd, c.epOut, 2, s_cmdBuf, writeDummyCb, nullptr);
            SYS_Report("[Gamepad] Switch Pro USB handshake sent (ep 0x%02x)\n", c.epOut);
        }
    }

    s_dev.fd        = fd;
    s_dev.ep        = c.ep;
    s_dev.epOut     = c.epOut;
    s_dev.len       = c.len > 0 && c.len <= MAX_REPORT ? c.len : (c.type == PadType::DualShock3 ? 49 : 64);
    s_dev.type      = c.type;
    s_dev.vid       = c.dev.vid;
    s_dev.pid       = c.dev.pid;
    s_dev.connected = true;

    const char* typeName = "Generic USB Gamepad";
    if (c.type == PadType::DualShock4) typeName = "DualShock 4";
    else if (c.type == PadType::DualShock3) typeName = "DualShock 3";
    else if (c.type == PadType::XboxOne) typeName = "Xbox One / Series Controller";
    else if (c.type == PadType::Xbox360) typeName = "Xbox 360 Controller";
    else if (c.type == PadType::SwitchPro) typeName = "Nintendo Switch Pro Controller";

    SYS_Report("[Gamepad] Connected %s (VID 0x%04x PID 0x%04x, len %d)\n",
               typeName, c.dev.vid, c.dev.pid, (int)s_dev.len);
    return true;
}

void decodeDualShock4(const u8* buf, int len);
void decodeDualShock3(const u8* buf, int len);
void decodeXbox(const u8* buf, int len);
void decodeSwitchPro(const u8* buf, int len);
void decodeGenericHID(const u8* buf, int len);

void decodeDualShock4(const u8* buf, int len)
{
    if (len < 8) return;

    /* Detect report structure accurately:
     * When Report ID 0x01 is included, buf[0] is 0x01, sticks are buf[1..4],
     * and D-pad hat switch is buf[5] & 0x0F (neutral value 8, 0..7 when pressed).
     * If Report ID is stripped by host/adapter, sticks are buf[0..3] and hat is buf[4] & 0x0F. */
    int offset = 0;
    if (len >= 9 && buf[0] == 0x01 && (buf[5] & 0x0F) <= 8) {
        offset = 1;
    } else if (len >= 8 && (buf[4] & 0x0F) <= 8) {
        offset = 0;
    } else if (len >= 9 && buf[0] == 0x01) {
        offset = 1;
    }

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
    if (b6 & (1 << 0)) held |= WPAD_BUTTON_MINUS;    /* L1 -> Prev page / L */
    if (b6 & (1 << 1)) held |= WPAD_BUTTON_PLUS;     /* R1 -> Next page / R */
    if (b6 & (1 << 2)) held |= WPAD_BUTTON_MINUS;    /* L2 -> Prev page / L */
    if (b6 & (1 << 3)) held |= WPAD_BUTTON_PLUS;     /* R2 -> Next page / R */
    if (b6 & (1 << 4)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Share / Select */
    if (b6 & (1 << 5)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Options / Start */
    if (b6 & (1 << 6)) held |= 0;                    /* L3 */
    if (b6 & (1 << 7)) held |= Input::BTN_ZOOM;      /* R3 -> Zoom */

    /* PS Home button (bit 0 of next byte) */
    if (len >= offset + 7) {
        const u8 b7 = buf[offset + 6];
        if (b7 & (1 << 0)) held |= WPAD_BUTTON_HOME;     /* PS Button */
    }
    /* Share + Options combo as fallback Home */
    if ((b6 & (1 << 4)) && (b6 & (1 << 5))) held |= WPAD_BUTTON_HOME;

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
    if (b2 & (1 << 1)) held |= 0;                     /* L3 */
    if (b2 & (1 << 2)) held |= Input::BTN_ZOOM;       /* R3 -> Zoom */
    if (b2 & (1 << 3)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Start */
    if (b2 & (1 << 4)) held |= (WPAD_BUTTON_UP | Input::BTN_DPAD_UP);
    if (b2 & (1 << 5)) held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT);
    if (b2 & (1 << 6)) held |= (WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN);
    if (b2 & (1 << 7)) held |= (WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT);

    /* Triggers & face buttons in buf[3] */
    const u8 b3 = buf[3];
    if (b3 & (1 << 0)) held |= WPAD_BUTTON_MINUS;    /* L2 -> Prev page / L */
    if (b3 & (1 << 1)) held |= WPAD_BUTTON_PLUS;     /* R2 -> Next page / R */
    if (b3 & (1 << 2)) held |= WPAD_BUTTON_MINUS;    /* L1 -> Prev page / L */
    if (b3 & (1 << 3)) held |= WPAD_BUTTON_PLUS;     /* R1 -> Next page / R */
    if (b3 & (1 << 4)) held |= WPAD_BUTTON_2;        /* Triangle -> X */
    if (b3 & (1 << 5)) held |= WPAD_BUTTON_B;        /* Circle -> B */
    if (b3 & (1 << 6)) held |= WPAD_BUTTON_A;        /* Cross -> A */
    if (b3 & (1 << 7)) held |= WPAD_BUTTON_1;        /* Square -> Y */

    /* PS Button in buf[4] */
    if (len >= 5) {
        const u8 b4 = buf[4];
        if (b4 & (1 << 0)) held |= WPAD_BUTTON_HOME;     /* PS Button */
    }
    /* Select + Start combo as fallback Home */
    if ((b2 & (1 << 0)) && (b2 & (1 << 3))) held |= WPAD_BUTTON_HOME;

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

void decodeXbox(const u8* buf, int len)
{
    if (len < 4) return;

    /* Acknowledge GIP packets that request ACK or announce mode/presence */
    if (buf[0] == 0x07 || buf[0] == 0x02 || (len >= 2 && (buf[1] & 0x10))) {
        if (s_cmdBuf && s_dev.epOut) {
            const u8 seq = (len >= 3) ? buf[2] : 0;
            const u8 cmd = buf[0];
            const u8 ack[13] = {
                0x01, 0x20, seq, 0x09,
                0x00, cmd,  0x20, 0x02,
                0x00, 0x00, 0x00, 0x00, 0x00
            };
            memcpy(s_cmdBuf, ack, sizeof(ack));
            USB_WriteIntrMsgAsync(s_dev.fd, s_dev.epOut, sizeof(ack), s_cmdBuf, writeDummyCb, nullptr);
        }
    }

    /* Xbox One: Announcement packet (re-send power-on sequence if needed) */
    if (buf[0] == 0x02) {
        if (s_cmdBuf && s_dev.epOut) {
            const u8 xboxone_init[5] = { 0x05, 0x20, 0x00, 0x01, 0x00 };
            memcpy(s_cmdBuf, xboxone_init, sizeof(xboxone_init));
            USB_WriteIntrMsgAsync(s_dev.fd, s_dev.epOut, sizeof(xboxone_init), s_cmdBuf, writeDummyCb, nullptr);
            const u8 xboxone_s_init[5] = { 0x05, 0x20, 0x01, 0x0f, 0x06 };
            memcpy(s_cmdBuf, xboxone_s_init, sizeof(xboxone_s_init));
            USB_WriteIntrMsgAsync(s_dev.fd, s_dev.epOut, sizeof(xboxone_s_init), s_cmdBuf, writeDummyCb, nullptr);
        }
        return;
    }

    /* Xbox One: Virtual Key / Guide button report */
    if (buf[0] == 0x07 && len >= 4) {
        const u8 state = (len >= 5) ? buf[4] : buf[3];
        s_xboxGuideHeld = (state & 0x01) != 0;
        LWP_MutexLock(s_lock);
        if (s_xboxGuideHeld) {
            if (!(s_held & WPAD_BUTTON_HOME)) s_pressedAccum |= WPAD_BUTTON_HOME;
            s_held |= WPAD_BUTTON_HOME;
        } else {
            s_held &= ~WPAD_BUTTON_HOME;
        }
        LWP_MutexUnlock(s_lock);
        return;
    }

    /* Xbox One: Standard GIP Input Report */
    if (buf[0] == 0x20 && len >= 18) {
        u32 held = 0;

        /* Buttons in buf[4] */
        const u8 b4 = buf[4];
        if (b4 & (1 << 2)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Menu (Start) */
        if (b4 & (1 << 3)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* View (Select) */
        if (b4 & (1 << 4)) held |= WPAD_BUTTON_A;                                  /* A */
        if (b4 & (1 << 5)) held |= WPAD_BUTTON_B;                                  /* B */
        if (b4 & (1 << 6)) held |= WPAD_BUTTON_1;                                  /* X */
        if (b4 & (1 << 7)) held |= WPAD_BUTTON_2;                                  /* Y */

        /* View + Menu combo -> Home */
        if ((b4 & (1 << 2)) && (b4 & (1 << 3))) held |= WPAD_BUTTON_HOME;

        /* D-pad and Bumpers in buf[5] */
        const u8 b5 = buf[5];
        if (b5 & (1 << 0)) held |= (WPAD_BUTTON_UP    | Input::BTN_DPAD_UP);
        if (b5 & (1 << 1)) held |= (WPAD_BUTTON_DOWN  | Input::BTN_DPAD_DOWN);
        if (b5 & (1 << 2)) held |= (WPAD_BUTTON_LEFT  | Input::BTN_DPAD_LEFT);
        if (b5 & (1 << 3)) held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT);
        if (b5 & (1 << 4)) held |= WPAD_BUTTON_MINUS;                              /* LB */
        if (b5 & (1 << 5)) held |= WPAD_BUTTON_PLUS;                               /* RB */
        if (b5 & (1 << 6)) held |= 0;                                              /* LS */
        if (b5 & (1 << 7)) held |= Input::BTN_ZOOM;                                /* RS -> Zoom */

        /* Triggers: LT (buf[6..7]), RT (buf[8..9]) - 0..1023 */
        const u16 lt = (u16)(buf[6] | (buf[7] << 8));
        const u16 rt = (u16)(buf[8] | (buf[9] << 8));
        if (lt > 250) held |= WPAD_BUTTON_MINUS;
        if (rt > 250) held |= WPAD_BUTTON_PLUS;

        /* Guide button state maintained from 0x07 packets */
        if (s_xboxGuideHeld) held |= WPAD_BUTTON_HOME;

        /* Analog sticks: signed 16-bit little-endian (scale >> 8 to -128..127) */
        const s16 rawLx = (s16)(buf[12] | (buf[13] << 8));
        const s16 rawLy = (s16)(buf[14] | (buf[15] << 8));
        const s16 rawRx = (s16)(buf[16] | (buf[17] << 8));
        const s16 rawRy = (s16)(buf[18] | (buf[19] << 8));

        const s8 sx = (s8)(rawLx >> 8);
        const s8 sy = (s8)(rawLy >> 8);
        const s8 rx = (s8)(rawRx >> 8);
        const s8 ry = (s8)(rawRy >> 8);

        LWP_MutexLock(s_lock);
        const u32 pressed = held & ~s_held;
        s_pressedAccum |= pressed;
        s_held   = held;
        s_stickX = sx;
        s_stickY = sy;
        s_subX   = rx;
        s_subY   = ry;
        LWP_MutexUnlock(s_lock);
        return;
    }

    /* Xbox 360: Standard 20-byte report (buf[0] == 0x00, buf[1] == 0x14) */
    if (buf[0] == 0x00 && buf[1] == 0x14 && len >= 14) {
        u32 held = 0;
        const u8 b2 = buf[2];
        if (b2 & (1 << 0)) held |= (WPAD_BUTTON_UP    | Input::BTN_DPAD_UP);
        if (b2 & (1 << 1)) held |= (WPAD_BUTTON_DOWN  | Input::BTN_DPAD_DOWN);
        if (b2 & (1 << 2)) held |= (WPAD_BUTTON_LEFT  | Input::BTN_DPAD_LEFT);
        if (b2 & (1 << 3)) held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT);
        if (b2 & (1 << 4)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Start */
        if (b2 & (1 << 5)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Back */
        if (b2 & (1 << 6)) held |= 0;                                              /* LS */
        if (b2 & (1 << 7)) held |= Input::BTN_ZOOM;                                /* RS -> Zoom */

        /* Back + Start combo -> Home */
        if ((b2 & (1 << 4)) && (b2 & (1 << 5))) held |= WPAD_BUTTON_HOME;

        const u8 b3 = buf[3];
        if (b3 & (1 << 0)) held |= WPAD_BUTTON_MINUS;                              /* LB */
        if (b3 & (1 << 1)) held |= WPAD_BUTTON_PLUS;                               /* RB */
        if (b3 & (1 << 2)) held |= WPAD_BUTTON_HOME;                               /* Xbox Guide */
        if (b3 & (1 << 4)) held |= WPAD_BUTTON_A;                                  /* A */
        if (b3 & (1 << 5)) held |= WPAD_BUTTON_B;                                  /* B */
        if (b3 & (1 << 6)) held |= WPAD_BUTTON_1;                                  /* X */
        if (b3 & (1 << 7)) held |= WPAD_BUTTON_2;                                  /* Y */

        /* Triggers in buf[4] and buf[5] */
        if (buf[4] > 30) held |= WPAD_BUTTON_MINUS;
        if (buf[5] > 30) held |= WPAD_BUTTON_PLUS;

        /* Sticks in buf[6..13] */
        const s16 rawLx = (s16)(buf[6]  | (buf[7]  << 8));
        const s16 rawLy = (s16)(buf[8]  | (buf[9]  << 8));
        const s16 rawRx = (s16)(buf[10] | (buf[11] << 8));
        const s16 rawRy = (s16)(buf[12] | (buf[13] << 8));

        const s8 sx = (s8)(rawLx >> 8);
        const s8 sy = (s8)(rawLy >> 8);
        const s8 rx = (s8)(rawRx >> 8);
        const s8 ry = (s8)(rawRy >> 8);

        LWP_MutexLock(s_lock);
        const u32 pressed = held & ~s_held;
        s_pressedAccum |= pressed;
        s_held   = held;
        s_stickX = sx;
        s_stickY = sy;
        s_subX   = rx;
        s_subY   = ry;
        LWP_MutexUnlock(s_lock);
        return;
    }
}

void decodeSwitchPro(const u8* buf, int len)
{
    if (len < 8) return;

    /* Report 0x30 / 0x21: Full standard report */
    if ((buf[0] == 0x30 || buf[0] == 0x21) && len >= 12) {
        u32 held = 0;

        /* buf[3]: Right Joy-Con buttons */
        const u8 b3 = buf[3];
        if (b3 & (1 << 0)) held |= WPAD_BUTTON_1;      /* Y */
        if (b3 & (1 << 1)) held |= WPAD_BUTTON_2;      /* X */
        if (b3 & (1 << 2)) held |= WPAD_BUTTON_B;      /* B */
        if (b3 & (1 << 3)) held |= WPAD_BUTTON_A;      /* A */
        if (b3 & (1 << 6)) held |= WPAD_BUTTON_PLUS;   /* R */
        if (b3 & (1 << 7)) held |= WPAD_BUTTON_PLUS;   /* ZR */

        /* buf[4]: System & stick buttons */
        const u8 b4 = buf[4];
        if (b4 & (1 << 0)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Minus (-) */
        if (b4 & (1 << 1)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Plus (+) */
        if (b4 & (1 << 2)) held |= Input::BTN_ZOOM;    /* R3 -> Zoom */
        if (b4 & (1 << 3)) held |= 0;                  /* L3 */
        if (b4 & (1 << 4)) held |= WPAD_BUTTON_HOME;   /* Home */
        if (b4 & (1 << 5)) held |= 0;                  /* Capture */

        /* Minus + Plus combo -> Home fallback */
        if ((b4 & (1 << 0)) && (b4 & (1 << 1))) held |= WPAD_BUTTON_HOME;

        /* buf[5]: Left Joy-Con buttons / D-pad */
        const u8 b5 = buf[5];
        if (b5 & (1 << 0)) held |= (WPAD_BUTTON_DOWN  | Input::BTN_DPAD_DOWN);
        if (b5 & (1 << 1)) held |= (WPAD_BUTTON_UP    | Input::BTN_DPAD_UP);
        if (b5 & (1 << 2)) held |= (WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT);
        if (b5 & (1 << 3)) held |= (WPAD_BUTTON_LEFT  | Input::BTN_DPAD_LEFT);
        if (b5 & (1 << 6)) held |= WPAD_BUTTON_MINUS;  /* L */
        if (b5 & (1 << 7)) held |= WPAD_BUTTON_MINUS;  /* ZL */

        /* 12-bit analog sticks in buf[6..11] */
        const u16 rawLx = (u16)(buf[6] | ((buf[7] & 0x0F) << 8));
        const u16 rawLy = (u16)((buf[7] >> 4) | (buf[8] << 4));
        const u16 rawRx = (u16)(buf[9] | ((buf[10] & 0x0F) << 8));
        const u16 rawRy = (u16)((buf[10] >> 4) | (buf[11] << 4));

        int valLx = ((int)rawLx - 2048) / 16;
        int valLy = ((int)rawLy - 2048) / 16;
        int valRx = ((int)rawRx - 2048) / 16;
        int valRy = ((int)rawRy - 2048) / 16;

        if (valLx < -128) valLx = -128; else if (valLx > 127) valLx = 127;
        if (valLy < -128) valLy = -128; else if (valLy > 127) valLy = 127;
        if (valRx < -128) valRx = -128; else if (valRx > 127) valRx = 127;
        if (valRy < -128) valRy = -128; else if (valRy > 127) valRy = 127;

        LWP_MutexLock(s_lock);
        const u32 pressed = held & ~s_held;
        s_pressedAccum |= pressed;
        s_held   = held;
        s_stickX = (s8)valLx;
        s_stickY = (s8)valLy;
        s_subX   = (s8)valRx;
        s_subY   = (s8)valRy;
        LWP_MutexUnlock(s_lock);
        return;
    }

    /* Report 0x3F: Simple HID input report */
    if (buf[0] == 0x3F && len >= 8) {
        u32 held = 0;
        const u8 b1 = buf[1];
        if (b1 & (1 << 0)) held |= WPAD_BUTTON_1;      /* Y */
        if (b1 & (1 << 1)) held |= WPAD_BUTTON_B;      /* B */
        if (b1 & (1 << 2)) held |= WPAD_BUTTON_A;      /* A */
        if (b1 & (1 << 3)) held |= WPAD_BUTTON_2;      /* X */
        if (b1 & (1 << 4)) held |= WPAD_BUTTON_MINUS;  /* L */
        if (b1 & (1 << 5)) held |= WPAD_BUTTON_PLUS;   /* R */
        if (b1 & (1 << 6)) held |= WPAD_BUTTON_MINUS;  /* ZL */
        if (b1 & (1 << 7)) held |= WPAD_BUTTON_PLUS;   /* ZR */

        const u8 b2 = buf[2];
        if (b2 & (1 << 0)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Minus (-) */
        if (b2 & (1 << 1)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Plus (+) */
        if (b2 & (1 << 2)) held |= 0;                  /* L3 */
        if (b2 & (1 << 3)) held |= Input::BTN_ZOOM;    /* R3 -> Zoom */
        if (b2 & (1 << 4)) held |= WPAD_BUTTON_HOME;   /* Home */

        if ((b2 & (1 << 0)) && (b2 & (1 << 1))) held |= WPAD_BUTTON_HOME;

        /* Hat switch in buf[3] */
        const u8 hat = buf[3] & 0x0F;
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

        /* 8-bit analog sticks in buf[4..7] */
        const s8 sx = (s8)((int)buf[4] - 128);
        const s8 sy = (s8)(128 - (int)buf[5]);
        const s8 rx = (s8)((int)buf[6] - 128);
        const s8 ry = (s8)(128 - (int)buf[7]);

        LWP_MutexLock(s_lock);
        const u32 pressed = held & ~s_held;
        s_pressedAccum |= pressed;
        s_held   = held;
        s_stickX = sx;
        s_stickY = sy;
        s_subX   = rx;
        s_subY   = ry;
        LWP_MutexUnlock(s_lock);
        return;
    }

    /* Fallback to generic HID parser */
    decodeGenericHID(buf, len);
}

void decodeGenericHID(const u8* buf, int len)
{
    if (len < 5) return;

    /* If this report actually matches Xbox One GIP input packet */
    if ((buf[0] == 0x20 && len >= 18) || (buf[0] == 0x07 && len >= 5) || (buf[0] == 0x00 && buf[1] == 0x14 && len >= 14)) {
        s_dev.type = (buf[0] == 0x00 && buf[1] == 0x14) ? PadType::Xbox360 : PadType::XboxOne;
        decodeXbox(buf, len);
        return;
    }
    /* If matches Switch Pro report */
    if (((buf[0] == 0x30 || buf[0] == 0x21) && len >= 12) || (buf[0] == 0x3F && len >= 8)) {
        s_dev.type = PadType::SwitchPro;
        decodeSwitchPro(buf, len);
        return;
    }
    /* If this report actually matches a DualShock 4 report (e.g. clone / third-party), route to DS4 parser */
    if (len >= 64 && buf[0] == 0x01 && (buf[5] & 0x0F) <= 8) {
        s_dev.type = PadType::DualShock4;
        decodeDualShock4(buf, len);
        return;
    }
    /* If matches DualShock 3 report */
    if (len >= 49 && buf[0] == 0x01 && buf[1] == 0x00) {
        s_dev.type = PadType::DualShock3;
        decodeDualShock3(buf, len);
        return;
    }

    /* Check for report ID offset (0x01) */
    const int offset = (buf[0] == 0x01 && len >= 6) ? 1 : 0;

    /* Analog sticks: X/Y on buf[offset+0]/buf[offset+1], Z/Rz on buf[offset+2]/buf[offset+3] */
    const s8 sx = (s8)((int)buf[offset + 0] - 128);
    const s8 sy = (s8)(128 - (int)buf[offset + 1]);
    s8 rx = 0, ry = 0;
    if (len >= offset + 4) {
        rx = (s8)((int)buf[offset + 2] - 128);
        ry = (s8)(128 - (int)buf[offset + 3]);
    }

    u32 held = 0;

    /* Identify DragonRise / Shanwan-like layout vs DirectInput */
    const bool isDragonRiseLike = (s_dev.vid == 0x0079 || s_dev.vid == 0x2563 ||
                                   s_dev.vid == 0x0810 || s_dev.vid == 0x11ff ||
                                   s_dev.vid == 0x0e8f ||
                                   (len >= offset + 5 && ((buf[offset + 4] & 0x0F) == 8 || (buf[offset + 4] & 0x0F) == 15)));

    if (isDragonRiseLike && len >= offset + 5) {
        /* Hat switch in lower 4 bits of buf[offset + 4] */
        const u8 hat = buf[offset + 4] & 0x0F;
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

        /* Face buttons in upper 4 bits of buf[offset + 4] */
        const u8 b4 = (buf[offset + 4] >> 4) & 0x0F;
        if (b4 & (1 << 0)) held |= WPAD_BUTTON_2;      /* Button 1 (Triangle / X) */
        if (b4 & (1 << 1)) held |= WPAD_BUTTON_B;      /* Button 2 (Circle / B) */
        if (b4 & (1 << 2)) held |= WPAD_BUTTON_A;      /* Button 3 (Cross / A) */
        if (b4 & (1 << 3)) held |= WPAD_BUTTON_1;      /* Button 4 (Square / Y) */

        /* Shoulders & extra buttons in buf[offset + 5] */
        if (len >= offset + 6) {
            const u8 b5 = buf[offset + 5];
            if (b5 & (1 << 0)) held |= WPAD_BUTTON_MINUS; /* Button 5 (L1) -> Prev page / L */
            if (b5 & (1 << 1)) held |= WPAD_BUTTON_PLUS;  /* Button 6 (R1) -> Next page / R */
            if (b5 & (1 << 2)) held |= WPAD_BUTTON_MINUS; /* Button 7 (L2) -> Prev page / L */
            if (b5 & (1 << 3)) held |= WPAD_BUTTON_PLUS;  /* Button 8 (R2) -> Next page / R */
            if (b5 & (1 << 4)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Button 9 (Select) */
            if (b5 & (1 << 5)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Button 10 (Start) */
            if (b5 & (1 << 6)) held |= 0;                 /* Button 11 (L3) */
            if (b5 & (1 << 7)) held |= Input::BTN_ZOOM;   /* Button 12 (R3) -> Zoom */

            /* Select + Start together triggers Home */
            if ((b5 & (1 << 4)) && (b5 & (1 << 5))) held |= WPAD_BUTTON_HOME;
        }
    } else {
        /* Standard DirectInput gamepad layout */
        if (len >= offset + 5) {
            const u8 b4 = buf[offset + 4];
            if (b4 & (1 << 0)) held |= WPAD_BUTTON_A;      /* Button 1 (A) */
            if (b4 & (1 << 1)) held |= WPAD_BUTTON_B;      /* Button 2 (B) */
            if (b4 & (1 << 2)) held |= WPAD_BUTTON_1;      /* Button 3 (X) */
            if (b4 & (1 << 3)) held |= WPAD_BUTTON_2;      /* Button 4 (Y) */
            if (b4 & (1 << 4)) held |= WPAD_BUTTON_MINUS;  /* Button 5 (L1) -> Prev page / L */
            if (b4 & (1 << 5)) held |= WPAD_BUTTON_PLUS;   /* Button 6 (R1) -> Next page / R */
            if (b4 & (1 << 6)) held |= WPAD_BUTTON_MINUS;  /* Button 7 (L2) -> Prev page / L */
            if (b4 & (1 << 7)) held |= WPAD_BUTTON_PLUS;   /* Button 8 (R2) -> Next page / R */
        }
        if (len >= offset + 6) {
            const u8 b5 = buf[offset + 5];
            if (b5 & (1 << 0)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Button 9 (Select) */
            if (b5 & (1 << 1)) held |= (WPAD_CLASSIC_BUTTON_PLUS  | WPAD_BUTTON_PLUS);  /* Button 10 (Start) */
            if (b5 & (1 << 2)) held |= 0;                 /* Button 11 (L3) */
            if (b5 & (1 << 3)) held |= Input::BTN_ZOOM;   /* Button 12 (R3) -> Zoom */

            /* Select + Start together triggers Home */
            if ((b5 & (1 << 0)) && (b5 & (1 << 1))) held |= WPAD_BUTTON_HOME;
        }
        if (len >= offset + 7) {
            const u8 hat = buf[offset + 6] & 0x0F;
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
                /* Register change notify for HID devices just in case */
                USB_DeviceChangeNotifyAsync(USB_CLASS_HID, changeCb, nullptr);
                /* Sleep briefly before rescanning so any newly plugged device
                 * (HID or Vendor 0xFF) is picked up promptly without burning CPU */
                for (int w = 0; w < 5 && !s_quit; ++w) {
                    usleep(100000);
                }
                continue;
            }
        }

        if (s_quit) break;

        /* Submit interrupt read request */
        if (USB_ReadIntrMsgAsync(s_dev.fd, s_dev.ep, s_dev.len, s_buf, readCb, nullptr) < 0) {
            USB_CloseDevice(&s_dev.fd);
            s_dev.connected = false;
            s_xboxGuideHeld = false;
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
            } else if (s_dev.type == PadType::XboxOne || s_dev.type == PadType::Xbox360) {
                decodeXbox(s_buf, s_result);
            } else if (s_dev.type == PadType::SwitchPro) {
                decodeSwitchPro(s_buf, s_result);
            } else if (s_dev.type == PadType::GenericHID) {
                decodeGenericHID(s_buf, s_result);
            }
        } else if (s_result < 0) {
            SYS_Report("[Gamepad] Controller disconnected (%d)\n", (int)s_result);
            USB_CloseDevice(&s_dev.fd);
            s_dev.connected = false;
            s_xboxGuideHeld = false;
            LWP_MutexLock(s_lock);
            s_held = 0;
            s_stickX = s_stickY = s_subX = s_subY = 0;
            LWP_MutexUnlock(s_lock);
        }
    }

    if (s_dev.fd >= 0) {
        USB_CloseDevice(&s_dev.fd);
        s_dev.connected = false;
        s_xboxGuideHeld = false;
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
    if (!s_cmdBuf) s_cmdBuf = (u8*)iosAlloc(s_heap, 64);
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

bool UsbGamepad::isXbox()
{
    return s_dev.connected && (s_dev.type == PadType::XboxOne || s_dev.type == PadType::Xbox360);
}
