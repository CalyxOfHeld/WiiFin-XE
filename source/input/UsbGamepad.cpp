#include "UsbGamepad.h"
#include "UsbMouse.h"
#include "Input.h"
#include <gccore.h>
#include <ogc/usb.h>
#include <ogc/ipc.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/semaphore.h>
#include <ogc/system.h>
#include <wiiuse/wpad.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>

namespace {

const int MAX_DEVICES = 16;
const int MAX_REPORT  = 64;
const int MAX_PADS    = 4;          /* pads read at once, one thread each */
const int STACK_SIZE  = 16 * 1024;

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
    s32              fd;     /* open already (a vendor device), or -1 */
};

/* One pad slot: its device, its thread, its state */
struct Pad {
    bool    connected = false;
    s32     fd = -1;
    s32     device = -1;    /* its usb_device_entry id */
    u8      iface = 0;
    u8      ep = 0;
    u8      epOut = 0;
    u16     len = 64;
    PadType type = PadType::None;
    u16     vid = 0;
    u16     pid = 0;

    u8*     buf = nullptr;  /* 32-aligned, IOS heap: interrupt reads */
    u8*     cmd = nullptr;  /* 32-aligned, IOS heap: what is sent to the pad */
    u8*     stack = nullptr;
    lwp_t   thread = LWP_THREAD_NULL;
    sem_t   readSema = LWP_SEM_NULL;
    sem_t   waitSema = LWP_SEM_NULL;

    volatile s32  result = 0;
    volatile bool readDone = false;  /* the read asked for has finished */
    volatile bool removed = false;   /* the removal callback came */
    volatile bool hidWatch = false;  /* a device change notification is asked for */
    volatile bool venWatch = false;
    bool    rehook = false;          /* the removal callback to ask for again */
    int     dumped = 0;              /* reports logged since connecting */
    bool    guideHeld = false;       /* Xbox One: from its 0x07 reports */
    u8      seq = 0;                 /* packets sent: their counter */
    u32     reports = 0;             /* Xbox One: reports since its last status */

    /* what take() hands out (s_lock) */
    u32 held = 0;
    u32 pressed = 0;
    s8  sx = 0, sy = 0, rx = 0, ry = 0;
};

/* One report, read */
struct Report {
    u32 held = 0;
    s8  sx = 0, sy = 0, rx = 0, ry = 0;
};

s32           s_heap = -1;
Pad           s_pads[MAX_PADS];
mutex_t       s_lock = LWP_MUTEX_NULL;   /* the pads' state */
mutex_t       s_claim = LWP_MUTEX_NULL;  /* a scan and the pad it opens */
volatile bool s_quit = false;
PadType       s_lastType = PadType::None; /* the pad used last: its glyphs */

s32 readCb(s32 result, void* arg)
{
    Pad* p = (Pad*)arg;
    p->result = result;
    p->readDone = true;
    LWP_SemPost(p->readSema);
    return 0;
}

s32 removedCb(s32, void* arg)
{
    Pad* p = (Pad*)arg;
    p->removed = true;
    LWP_SemPost(p->readSema);
    LWP_SemPost(p->waitSema);
    return 0;
}

/* a device came or went: the notification is used up */
s32 hidChangeCb(s32, void* arg)
{
    Pad* p = (Pad*)arg;
    p->hidWatch = false;
    LWP_SemPost(p->waitSema);
    return 0;
}

s32 venChangeCb(s32, void* arg)
{
    Pad* p = (Pad*)arg;
    p->venWatch = false;
    LWP_SemPost(p->waitSema);
    return 0;
}

/* A vendor-class (0xFF) interface: an Xbox pad, or None.  Only these are
 * Xbox pads: the vendors below also make HID pads (PDP, PowerA and Hori
 * for Switch, Razer for PS4), which are not. */
PadType identifyXbox(u16 vid, u16 pid, u8 ifaceSubClass, u8 ifaceProto)
{
    if (ifaceSubClass == 0x47 || ifaceProto == 0xD0) {
        return PadType::XboxOne;
    }
    if (ifaceSubClass == 0x5D || ifaceProto == 0x01) {
        return PadType::Xbox360;
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
    /* another vendor-class interface: not a pad we know */
    return PadType::None;
}

PadType identifyPad(u16 vid, u16 pid, u8 ifaceClass, u8 ifaceSubClass, u8 ifaceProto)
{
    if (ifaceClass == 0xFF) return identifyXbox(vid, pid, ifaceSubClass, ifaceProto);

    /* Nintendo Switch controllers */
    if (vid == 0x057E) {
        if (pid == 0x2009 || pid == 0x2006 || pid == 0x2007 || pid == 0x200E ||
            pid == 0x2017 || pid == 0x2019 || pid == 0x201E) {
            return PadType::SwitchPro;
        }
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

const char* typeName(PadType t)
{
    switch (t) {
    case PadType::DualShock4: return "DualShock 4";
    case PadType::DualShock3: return "DualShock 3";
    case PadType::XboxOne:    return "Xbox One / Series Controller";
    case PadType::Xbox360:    return "Xbox 360 Controller";
    case PadType::SwitchPro:  return "Nintendo Switch Pro Controller";
    default:                  return "Generic USB Gamepad";
    }
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

/* A device one of the pads is reading (s_claim held) */
bool ownedByPad(s32 device)
{
    for (const Pad& p : s_pads)
        if (p.connected && p.device == device) return true;
    return false;
}

/* Devices looked at and found no pad (s_claim held): not looked at again
 * while they stay plugged in.  An Xbox pad's other two interfaces are
 * listed as devices of their own, and every idle pad thread read them
 * again at each scan, while the pad's own interface was streaming */
s32 s_notPad[MAX_DEVICES];
int s_notPadCount = 0;

bool isNotPad(s32 device)
{
    for (int i = 0; i < s_notPadCount; ++i)
        if (s_notPad[i] == device) return true;
    return false;
}

/* The pads we could open (s_claim held) */
int scanCandidates(Candidate* c, int max)
{
    u8 countHid = 0, countVendor = 0;
    usb_device_entry list[MAX_DEVICES];
    memset(list, 0, sizeof(list));

    /* Query both standard HID class (0x03) and Vendor Specific class (0xFF, used by Xbox) */
    if (USB_GetDeviceList(list, MAX_DEVICES, USB_CLASS_HID, &countHid) < 0) {
        countHid = 0;
    }
    if (countHid < MAX_DEVICES) {
        if (USB_GetDeviceList(list + countHid, MAX_DEVICES - countHid, 0xFF, &countVendor) < 0) {
            countVendor = 0;
        }
    }
    const int count = countHid + countVendor;

    /* what is plugged in, each time it changes: for controller bug reports */
    static u32 lastSig = ~0u;
    u32 sig = (u32)count;
    for (int i = 0; i < count; ++i) sig = sig * 31 + (u32)list[i].device_id + ((u32)list[i].vid << 16) + list[i].pid;
    const bool logList = sig != lastSig;
    lastSig = sig;
    if (logList) SYS_Report("[Gamepad] USB devices: %d HID, %d other\n", (int)countHid, (int)countVendor);

    /* the ones unplugged leave the list of non-pads */
    int kept = 0;
    for (int j = 0; j < s_notPadCount; ++j) {
        bool present = false;
        for (int i = 0; i < count && !present; ++i) present = list[i].device_id == s_notPad[j];
        if (present) s_notPad[kept++] = s_notPad[j];
    }
    s_notPadCount = kept;
    if (count == 0) return 0;

    int nc = 0;
    for (int i = 0; i < count && nc < max; ++i) {
        const usb_device_entry& e = list[i];
        if (!e.vid || !e.pid) continue;
        const bool vendorList = i >= countHid;
        /* the mouse driver's, or a pad being read: closing it after a look
         * would fire its removal callback (USBV5_CloseDevice does) */
        if (e.device_id == UsbMouse::deviceId() || ownedByPad(e.device_id)) {
            if (logList) SYS_Report("[Gamepad]   %04x:%04x %s, in use\n", e.vid, e.pid, vendorList ? "other" : "HID");
            continue;
        }
        if (isNotPad(e.device_id)) continue;

        usb_devdesc dd;
        memset(&dd, 0, sizeof(dd));
        /* A vendor device plugged in after start-up tells nothing until it
         * is opened (-4).  Only known Xbox pads are: the vendor list also
         * holds USB drives, whose driver would be told they went.  They are
         * opened at once, with no look first (0.2 s of it): one nobody
         * configures resets itself and comes back, an Xbox Series pad
         * every 0.6 s.  The one opened stays open for openCandidate. */
        const bool xboxVendor = vendorList && identifyXbox(e.vid, e.pid, 0, 0) != PadType::None;
        s32 got = !xboxVendor && (e.device_id >= 0x20 || e.device_id < -1) ? USB_GetDescriptors(e.device_id, &dd) : -1;
        s32 openFd = -1;
        if (got < 0 && (!vendorList || xboxVendor)) {
            s32 fd = -1;
            const s32 opened = USB_OpenDevice(e.device_id, e.vid, e.pid, &fd);
            if (opened >= 0) {
                memset(&dd, 0, sizeof(dd));
                got = USB_GetDescriptors(fd, &dd);
                if (vendorList && got >= 0) openFd = fd;
                else USB_CloseDevice(&fd);
            }
            if (xboxVendor) SYS_Report("[Gamepad]   %04x:%04x other: open %d, descriptors %d\n",
                                       e.vid, e.pid, (int)opened, opened >= 0 ? (int)got : 0);
        }
        if (got < 0 || dd.bNumConfigurations < 1) {
            if (logList) SYS_Report("[Gamepad]   %04x:%04x %s: no descriptors (%d)\n",
                                    e.vid, e.pid, vendorList ? "other" : "HID", (int)got);
            if (got >= 0) USB_FreeDescriptors(&dd);
            if (openFd >= 0) USB_CloseDevice(&openFd);
            /* a vendor device that is no Xbox pad (the USB Ethernet
             * adapter): not asked again at every scan */
            if (vendorList && !xboxVendor && s_notPadCount < MAX_DEVICES) s_notPad[s_notPadCount++] = e.device_id;
            continue;
        }
        const int before = nc;

        for (int conf = 0; conf < dd.bNumConfigurations && nc < max; ++conf) {
            const usb_configurationdesc& cd = dd.configurations[conf];
            for (int f = 0; f < cd.bNumInterfaces && cd.interfaces && nc < max; ++f) {
                const usb_interfacedesc* id = &cd.interfaces[f];
                const usb_endpointdesc* edIn = inEndpoint(id);
                if (logList)
                    SYS_Report("[Gamepad]   %04x:%04x interface %d: class %02x/%02x/%02x, in 0x%02x\n",
                               e.vid, e.pid, id->bInterfaceNumber, id->bInterfaceClass,
                               id->bInterfaceSubClass, id->bInterfaceProtocol, edIn ? edIn->bEndpointAddress : 0);
                if (!edIn) continue;

                /* Skip mice and keyboards */
                if (id->bInterfaceClass == USB_CLASS_HID &&
                    (id->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD || id->bInterfaceProtocol == USB_PROTOCOL_MOUSE))
                    continue;

                if (id->bInterfaceClass != USB_CLASS_HID && id->bInterfaceClass != 0xFF) continue;
                const PadType pt = identifyPad(e.vid, e.pid, id->bInterfaceClass, id->bInterfaceSubClass, id->bInterfaceProtocol);
                if (pt == PadType::None) continue;
                /* Xbox controller data is on interface 0 */
                if (id->bInterfaceClass == 0xFF && id->bInterfaceNumber != 0) continue;

                const usb_endpointdesc* edOut = outEndpoint(id);
                const u8 epOutAddr = edOut ? (u8)edOut->bEndpointAddress : 0;

                c[nc++] = Candidate{ e, (u8)id->bInterfaceNumber, (u8)edIn->bEndpointAddress, epOutAddr, (u16)edIn->wMaxPacketSize, pt, openFd };
                openFd = -1;
            }
        }
        USB_FreeDescriptors(&dd);
        if (openFd >= 0) USB_CloseDevice(&openFd);
        if (nc == before && s_notPadCount < MAX_DEVICES) s_notPad[s_notPadCount++] = e.device_id;
    }
    return nc;
}

/* A report descriptor with a Generic Desktop Joystick or Gamepad
 * application collection.  Keyboards and mice, and the extra interfaces of
 * their wireless receivers, have none. */
bool isGamepadReport(const u8* d, int n)
{
    u32 page = 0, usage = 0;
    bool haveUsage = false;
    for (int i = 0; i < n; ) {
        const u8 p = d[i++];
        if (p == 0xFE) {                  /* long item */
            if (i >= n) break;
            i += 2 + d[i];
            continue;
        }
        const int sz = (p & 3) == 3 ? 4 : (p & 3);
        if (i + sz > n) break;
        u32 u = 0;
        for (int k = 0; k < sz; ++k) u |= (u32)d[i + k] << (8 * k);
        i += sz;
        const int type = (p >> 2) & 3, tag = p >> 4;
        if (type == 1 && tag == 0) {      /* usage page */
            page = u;
        } else if (type == 2 && tag == 0 && !haveUsage) {   /* the first usage */
            usage = sz == 4 ? u : (page << 16) | u;
            haveUsage = true;
        } else if (type == 0) {           /* main: locals end here */
            if (tag == 10 && u == 1 && haveUsage &&
                (usage == 0x00010004u || usage == 0x00010005u))
                return true;
            haveUsage = false;
        }
    }
    return false;
}

/* Sends to the pad: its interrupt OUT endpoint, or else (a HID pad without
 * one, or one that refuses) an output report through SET_REPORT */
s32 sendToPad(Pad& p, const u8* data, int len)
{
    memcpy(p.cmd, data, len);
    s32 r = -1;
    if (p.epOut) r = USB_WriteIntrMsg(p.fd, p.epOut, len, p.cmd);
    if (r < 0 && p.type != PadType::XboxOne && p.type != PadType::Xbox360)
        r = USB_WriteCtrlMsg(p.fd, USB_REQTYPE_INTERFACE_SET, USB_REQ_SETREPORT,
                             (2 << 8) | data[0], p.iface, len, p.cmd);
    return r;
}

/* What a pad needs to be told before it reports (its thread, once open) */
void activate(Pad& p)
{
    if (p.type == PadType::DualShock3) {
        /* DualShock 3 operational report activation: feature report 0xF4 */
        const u8 enableCmd[4] = { 0x42, 0x0c, 0x00, 0x00 };
        memcpy(p.cmd, enableCmd, sizeof(enableCmd));
        const s32 r = USB_WriteCtrlMsg(p.fd, USB_REQTYPE_INTERFACE_SET, USB_REQ_SETREPORT,
                                       (3 << 8) | 0xf4, p.iface, sizeof(enableCmd), p.cmd);
        SYS_Report("[Gamepad] DualShock 3 activation: %d\n", (int)r);
    } else if (p.type == PadType::XboxOne) {
        if (p.epOut) {
            /* GIP Power-On packet */
            const u8 powerOn[5] = { 0x05, 0x20, p.seq++, 0x01, 0x00 };
            const s32 r = sendToPad(p, powerOn, sizeof(powerOn));
            /* the One S and Elite 2: input only after this one too (xpad) */
            s32 r2 = 0;
            if (p.vid == 0x045E && (p.pid == 0x02EA || p.pid == 0x0B00)) {
                const u8 sInit[5] = { 0x05, 0x20, p.seq++, 0x0f, 0x06 };
                r2 = sendToPad(p, sInit, sizeof(sInit));
            }
            SYS_Report("[Gamepad] Xbox One power-on init (ep 0x%02x): %d %d\n", p.epOut, (int)r, (int)r2);
        }
    } else if (p.type == PadType::SwitchPro) {
        /* As Linux's hid-nintendo: the USB handshake (0x80 0x02), no USB
         * timeout (0x80 0x04), then full reports (subcommand 0x03, mode
         * 0x30).  0x80 0x03 sets the pad's own UART speed (not USB's) and is
         * left out.  The replies (0x81, 0x21) are read and skipped. */
        const u8 handshake[2] = { 0x80, 0x02 };
        const u8 noTimeout[2] = { 0x80, 0x04 };
        const s32 r1 = sendToPad(p, handshake, sizeof(handshake));
        usleep(20000);
        const s32 r2 = sendToPad(p, noTimeout, sizeof(noTimeout));
        usleep(20000);
        const u8 fullReports[12] = { 0x01, (u8)(p.seq++ & 0x0F), 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x03, 0x30 };
        const s32 r3 = sendToPad(p, fullReports, sizeof(fullReports));
        SYS_Report("[Gamepad] Switch Pro USB handshake (ep 0x%02x): %d %d %d\n", p.epOut, (int)r1, (int)r2, (int)r3);
    }
}

/* Opens one candidate into a pad slot (s_claim held) */
bool openCandidate(Pad& p, Candidate& c)
{
    s32 fd = c.fd;
    c.fd = -1;
    if (fd < 0 && USB_OpenDevice(c.dev.device_id, c.dev.vid, c.dev.pid, &fd) < 0) return false;

    if (c.type == PadType::GenericHID) {
        static u8 rdesc[1024];
        const s32 size = USB_GetReportDescriptorSize(fd, c.iface);
        if (size <= 0 || size > (s32)sizeof(rdesc) ||
            USB_GetReportDescriptor(fd, c.iface, rdesc, (u16)size) < 0 || !isGamepadReport(rdesc, size)) {
            SYS_Report("[Gamepad] %04x:%04x interface %d: not a gamepad (%d byte descriptor)\n",
                       c.dev.vid, c.dev.pid, c.iface, (int)size);
            USB_CloseDevice(&fd);
            return false;
        }
    }

    /* Vendor-class (Xbox) pads: configuration 1.  Not HID ones: IOS has
     * configured those, and a SET_CONFIGURATION under it resets them. */
    if (c.type == PadType::XboxOne || c.type == PadType::Xbox360)
        USB_SetConfiguration(fd, 1);

    if (USB_DeviceRemovalNotifyAsync(fd, removedCb, &p) < 0) {
        USB_CloseDevice(&fd);
        return false;
    }

    p.fd        = fd;
    p.device    = c.dev.device_id;
    p.iface     = c.iface;
    p.ep        = c.ep;
    p.epOut     = c.epOut;
    p.len       = c.len > 0 && c.len <= MAX_REPORT ? c.len : (c.type == PadType::DualShock3 ? 49 : 64);
    p.type      = c.type;
    p.vid       = c.dev.vid;
    p.pid       = c.dev.pid;
    p.dumped    = 0;
    p.reports   = 0;
    p.removed   = false;
    p.rehook    = false;
    p.guideHeld = false;
    /* closing the last pad fired its removal callback: that post would
     * start a second read under the first one */
    u32 posted = 0;
    if (LWP_SemGetValue(p.readSema, &posted) >= 0 && posted) LWP_SemWait(p.readSema);
    p.connected = true;

    SYS_Report("[Gamepad] Pad %d: connected %s (VID 0x%04x PID 0x%04x, len %d)\n",
               (int)(&p - s_pads) + 1, typeName(c.type), c.dev.vid, c.dev.pid, (int)p.len);
    return true;
}

/* A report read: the pad's state, a press of what was not held */
void commit(Pad& p, const Report& r)
{
    LWP_MutexLock(s_lock);
    const u32 pressed = r.held & ~p.held;
    p.pressed |= pressed;
    p.held = r.held;
    p.sx = r.sx;
    p.sy = r.sy;
    p.rx = r.rx;
    p.ry = r.ry;
    const int T = 48;
    if (pressed || r.sx > T || r.sx < -T || r.sy > T || r.sy < -T ||
        r.rx > T || r.rx < -T || r.ry > T || r.ry < -T)
        s_lastType = p.type;
    LWP_MutexUnlock(s_lock);
}

/* A hat switch (0 up, clockwise; 8 and up: none) */
u32 hatBits(u8 hat)
{
    switch (hat) {
    case 0: return WPAD_BUTTON_UP | Input::BTN_DPAD_UP;
    case 1: return WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT;
    case 2: return WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT;
    case 3: return WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_RIGHT | Input::BTN_DPAD_RIGHT;
    case 4: return WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN;
    case 5: return WPAD_BUTTON_DOWN | Input::BTN_DPAD_DOWN | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT;
    case 6: return WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT;
    case 7: return WPAD_BUTTON_UP | Input::BTN_DPAD_UP | WPAD_BUTTON_LEFT | Input::BTN_DPAD_LEFT;
    default: return 0;
    }
}

bool decodeDualShock4(const u8* buf, int len, Report& r)
{
    if (len < 8) return false;

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

    /* Convert to -128..127 with positive = up */
    r.sx = (s8)(buf[offset + 0] - 128);
    r.sy = (s8)(128 - buf[offset + 1]);
    r.rx = (s8)(buf[offset + 2] - 128);
    r.ry = (s8)(128 - buf[offset + 3]);

    u32 held = hatBits(buf[offset + 4] & 0x0F);

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
    if (b6 & (1 << 7)) held |= Input::BTN_ZOOM;      /* R3 -> Zoom */

    /* PS Home button (bit 0 of next byte) */
    if (len >= offset + 7) {
        const u8 b7 = buf[offset + 6];
        if (b7 & (1 << 0)) held |= WPAD_BUTTON_HOME;     /* PS Button */
    }
    /* Share + Options combo as fallback Home */
    if ((b6 & (1 << 4)) && (b6 & (1 << 5))) held |= WPAD_BUTTON_HOME;

    r.held = held;
    return true;
}

bool decodeDualShock3(const u8* buf, int len, Report& r)
{
    if (len < 10) return false;

    /* D-pad & standard buttons in buf[2] */
    u32 held = 0;
    const u8 b2 = buf[2];
    if (b2 & (1 << 0)) held |= (WPAD_CLASSIC_BUTTON_MINUS | WPAD_BUTTON_MINUS); /* Select */
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
    if (buf[4] & (1 << 0)) held |= WPAD_BUTTON_HOME;
    /* Select + Start combo as fallback Home */
    if ((b2 & (1 << 0)) && (b2 & (1 << 3))) held |= WPAD_BUTTON_HOME;

    /* Analog sticks */
    r.sx = (s8)(buf[6] - 128);
    r.sy = (s8)(128 - buf[7]);
    r.rx = (s8)(buf[8] - 128);
    r.ry = (s8)(128 - buf[9]);
    r.held = held;
    return true;
}

/* An Xbox One report that asks for an acknowledgement (GIP option 0x10):
 * the pad sends it again until it gets one */
void ackXboxOne(Pad& p, const u8* buf)
{
    const u8 ack[13] = { 0x01, 0x20, buf[2], 0x09, 0x00, buf[0], 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
    sendToPad(p, ack, sizeof(ack));
}

bool decodeXbox(Pad& p, const u8* buf, int len, Report& r)
{
    if (len < 4) return false;

    /* Xbox One: its status, every 20 s or so.  How many reports came
     * between two: for bug reports of a pad that stops */
    if (buf[0] == 0x03 && p.dumped >= 4) {
        SYS_Report("[Gamepad] Pad %d: Xbox status %02x, %u reports since the last\n",
                   (int)(&p - s_pads) + 1, len >= 5 ? buf[4] : 0, (unsigned)p.reports);
        p.reports = 0;
    }

    /* Xbox One: Virtual Key / Guide button report */
    if (buf[0] == 0x07 && len >= 5) {
        if (buf[1] & 0x10) ackXboxOne(p, buf);
        p.guideHeld = (buf[4] & 0x01) != 0;
        /* the rest as the last report had it */
        LWP_MutexLock(s_lock);
        r.held = p.guideHeld ? (p.held | WPAD_BUTTON_HOME) : (p.held & ~WPAD_BUTTON_HOME);
        r.sx = p.sx; r.sy = p.sy; r.rx = p.rx; r.ry = p.ry;
        LWP_MutexUnlock(s_lock);
        return true;
    }

    /* Xbox One: GIP input report.  buf[0..3] the header (0x20, options,
     * sequence, length 14), then the payload: buttons 4-5, triggers 6-9,
     * sticks 10-17 */
    if (buf[0] == 0x20 && len >= 18) {
        u32 held = 0;

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
        if (b5 & (1 << 7)) held |= Input::BTN_ZOOM;                                /* RS -> Zoom */

        /* Triggers: LT (buf[6..7]), RT (buf[8..9]) - 0..1023 */
        const u16 lt = (u16)(buf[6] | (buf[7] << 8));
        const u16 rt = (u16)(buf[8] | (buf[9] << 8));
        if (lt > 250) held |= WPAD_BUTTON_MINUS;
        if (rt > 250) held |= WPAD_BUTTON_PLUS;

        /* Guide button state maintained from 0x07 packets */
        if (p.guideHeld) held |= WPAD_BUTTON_HOME;

        /* Analog sticks: signed 16-bit little-endian, positive = up (scale
         * >> 8 to -128..127) */
        r.sx = (s8)((s16)(buf[10] | (buf[11] << 8)) >> 8);
        r.sy = (s8)((s16)(buf[12] | (buf[13] << 8)) >> 8);
        r.rx = (s8)((s16)(buf[14] | (buf[15] << 8)) >> 8);
        r.ry = (s8)((s16)(buf[16] | (buf[17] << 8)) >> 8);
        r.held = held;
        return true;
    }

    /* Xbox One: anything else asking for an acknowledgement (its arrival,
     * its status) gets one */
    if (p.type == PadType::XboxOne && (buf[1] & 0x10)) {
        ackXboxOne(p, buf);
        return false;
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
        r.sx = (s8)((s16)(buf[6]  | (buf[7]  << 8)) >> 8);
        r.sy = (s8)((s16)(buf[8]  | (buf[9]  << 8)) >> 8);
        r.rx = (s8)((s16)(buf[10] | (buf[11] << 8)) >> 8);
        r.ry = (s8)((s16)(buf[12] | (buf[13] << 8)) >> 8);
        r.held = held;
        return true;
    }
    return false;
}

s8 clampStick(int v)
{
    return (s8)(v < -128 ? -128 : v > 127 ? 127 : v);
}

bool decodeSwitchPro(const u8* buf, int len, Report& r)
{
    if (len < 8) return false;

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
        if (b4 & (1 << 4)) held |= WPAD_BUTTON_HOME;   /* Home */

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

        /* 12-bit analog sticks in buf[6..11], positive = up */
        const int lx = buf[6] | ((buf[7] & 0x0F) << 8);
        const int ly = (buf[7] >> 4) | (buf[8] << 4);
        const int rx = buf[9] | ((buf[10] & 0x0F) << 8);
        const int ry = (buf[10] >> 4) | (buf[11] << 4);
        r.sx = clampStick((lx - 2048) / 16);
        r.sy = clampStick((ly - 2048) / 16);
        r.rx = clampStick((rx - 2048) / 16);
        r.ry = clampStick((ry - 2048) / 16);
        r.held = held;
        return true;
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
        if (b2 & (1 << 3)) held |= Input::BTN_ZOOM;    /* R3 -> Zoom */
        if (b2 & (1 << 4)) held |= WPAD_BUTTON_HOME;   /* Home */
        if ((b2 & (1 << 0)) && (b2 & (1 << 1))) held |= WPAD_BUTTON_HOME;

        held |= hatBits(buf[3] & 0x0F);

        /* 8-bit analog sticks in buf[4..7] */
        r.sx = (s8)(buf[4] - 128);
        r.sy = (s8)(128 - buf[5]);
        r.rx = (s8)(buf[6] - 128);
        r.ry = (s8)(128 - buf[7]);
        r.held = held;
        return true;
    }

    /* anything else (0x81 handshake replies, ...) carries no buttons: read
     * as a generic pad's report it pressed buttons at random */
    return false;
}

bool decodeGenericHID(Pad& p, const u8* buf, int len, Report& r)
{
    if (len < 5) return false;

    /* Xbox pads are never HID ones and a Switch Pro is known by its ids: no
     * guessing them from a report's first byte */
    /* If this report actually matches a DualShock 4 report (e.g. clone / third-party), route to DS4 parser */
    if (len >= 64 && buf[0] == 0x01 && (buf[5] & 0x0F) <= 8) {
        p.type = PadType::DualShock4;
        return decodeDualShock4(buf, len, r);
    }
    /* If matches DualShock 3 report */
    if (len >= 49 && buf[0] == 0x01 && buf[1] == 0x00) {
        p.type = PadType::DualShock3;
        return decodeDualShock3(buf, len, r);
    }

    /* Check for report ID offset (0x01) */
    const int offset = (buf[0] == 0x01 && len >= 6) ? 1 : 0;

    /* Analog sticks: X/Y on buf[offset+0]/buf[offset+1], Z/Rz on buf[offset+2]/buf[offset+3] */
    r.sx = (s8)((int)buf[offset + 0] - 128);
    r.sy = (s8)(128 - (int)buf[offset + 1]);
    if (len >= offset + 4) {
        r.rx = (s8)((int)buf[offset + 2] - 128);
        r.ry = (s8)(128 - (int)buf[offset + 3]);
    }

    u32 held = 0;

    /* Identify DragonRise / Shanwan-like layout vs DirectInput */
    const bool isDragonRiseLike = (p.vid == 0x0079 || p.vid == 0x2563 ||
                                   p.vid == 0x0810 || p.vid == 0x11ff ||
                                   p.vid == 0x0e8f ||
                                   (len >= offset + 5 && ((buf[offset + 4] & 0x0F) == 8 || (buf[offset + 4] & 0x0F) == 15)));

    if (isDragonRiseLike && len >= offset + 5) {
        /* Hat switch in lower 4 bits of buf[offset + 4] */
        held |= hatBits(buf[offset + 4] & 0x0F);

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
            if (b5 & (1 << 3)) held |= Input::BTN_ZOOM;   /* Button 12 (R3) -> Zoom */

            /* Select + Start together triggers Home */
            if ((b5 & (1 << 0)) && (b5 & (1 << 1))) held |= WPAD_BUTTON_HOME;
        }
        if (len >= offset + 7) held |= hatBits(buf[offset + 6] & 0x0F);
    }

    r.held = held;
    return true;
}

/* The device is still plugged in: in either host's list (IOS 58's; the
 * old interface has no device ids, so there it counts as gone) */
bool stillAttached(s32 device)
{
    if (device == 0 || device == -1) return false;
    usb_device_entry list[MAX_DEVICES];
    u8 count = 0;
    if (USB_GetDeviceList(list, MAX_DEVICES, 0, &count) < 0) return false;
    for (int i = 0; i < count && i < MAX_DEVICES; ++i)
        if (list[i].device_id == device) return true;
    return false;
}

void dropPad(Pad& p, const char* why, int code)
{
    SYS_Report("[Gamepad] Pad %d: gone (%s %d)\n", (int)(&p - s_pads) + 1, why, code);
    USB_CloseDevice(&p.fd);
    LWP_MutexLock(s_lock);
    p.connected = false;
    p.held = 0;
    p.sx = p.sy = p.rx = p.ry = 0;
    LWP_MutexUnlock(s_lock);
}

void* padMain(void* arg)
{
    Pad& p = *(Pad*)arg;
    while (!s_quit) {
        if (!p.connected) {
            LWP_MutexLock(s_claim);
            Candidate cand[MAX_DEVICES];
            const int n = scanCandidates(cand, MAX_DEVICES);
            for (int i = 0; i < n; ++i) {
                if (openCandidate(p, cand[i])) break;
            }
            /* the ones the scan opened and no pad took */
            for (int i = 0; i < n; ++i)
                if (cand[i].fd >= 0) USB_CloseDevice(&cand[i].fd);
            LWP_MutexUnlock(s_claim);
            if (!p.connected) {
                /* wait for a device to come or go, on either host.  One
                 * notification each, asked again only once it has come:
                 * libogc keeps each one asked for in its 16 KB USB heap until
                 * a device changes, and asking twice a second filled it in a
                 * few minutes (then no USB request worked) */
                if (!p.hidWatch)
                    p.hidWatch = USB_DeviceChangeNotifyAsync(USB_CLASS_HID, hidChangeCb, &p) >= 0;
                if (!p.venWatch)
                    p.venWatch = USB_DeviceChangeNotifyAsync(0xFF, venChangeCb, &p) >= 0;
                if (p.hidWatch || p.venWatch) LWP_SemWait(p.waitSema);
                else usleep(500000);
                continue;
            }
            activate(p);
        }

        if (s_quit) break;

        /* the removal callback, asked for again: not from its callback,
         * where libogc still holds the old one (and refuses) */
        if (p.rehook && USB_DeviceRemovalNotifyAsync(p.fd, removedCb, &p) >= 0) p.rehook = false;

        p.result = 0;
        p.readDone = false;
        const s32 started = USB_ReadIntrMsgAsync(p.fd, p.ep, p.len, p.buf, readCb, &p);
        if (started < 0) {
            dropPad(p, "read could not start", (int)started);
            continue;
        }

        /* until the read finishes.  The removal callback also comes when
         * another driver opens and closes the pad (USBV5_CloseDevice fires
         * it; on the Wii a DualShock 4 got one every 3 s or so): the pad
         * still in the device list, the read goes on */
        bool gone = false;
        while (!s_quit) {
            LWP_SemWait(p.readSema);
            if (p.removed) {
                p.removed = false;
                if (!stillAttached(p.device)) { gone = true; break; }
                p.rehook = true;
            }
            if (p.readDone) break;
        }
        if (s_quit) break;
        if (gone) {
            dropPad(p, "removed", 0);
            continue;
        }

        const int got = p.result;
        if (got > 0) ++p.reports;
        /* the first reports, for controller bug reports */
        if (got > 0 && p.dumped < 4) {
            char hex[3 * 24 + 1];
            const int n = got < 24 ? got : 24;
            for (int k = 0; k < n; ++k) {
                static const char* H = "0123456789abcdef";
                hex[3 * k] = H[p.buf[k] >> 4]; hex[3 * k + 1] = H[p.buf[k] & 15]; hex[3 * k + 2] = ' ';
            }
            hex[3 * n] = 0;
            SYS_Report("[Gamepad] Pad %d report %d (%d bytes): %s\n", (int)(&p - s_pads) + 1, p.dumped + 1, got, hex);
            ++p.dumped;
        }

        if (got > 0) {
            Report r;
            bool ok = false;
            if (p.type == PadType::DualShock4)      ok = decodeDualShock4(p.buf, got, r);
            else if (p.type == PadType::DualShock3) ok = decodeDualShock3(p.buf, got, r);
            else if (p.type == PadType::XboxOne || p.type == PadType::Xbox360) ok = decodeXbox(p, p.buf, got, r);
            else if (p.type == PadType::SwitchPro)  ok = decodeSwitchPro(p.buf, got, r);
            else if (p.type == PadType::GenericHID) ok = decodeGenericHID(p, p.buf, got, r);
            if (ok) commit(p, r);
        } else if (got < 0) {
            dropPad(p, "read failed", got);
        }
    }

    if (p.connected) dropPad(p, "shutdown", 0);
    return nullptr;
}

} // namespace

bool UsbGamepad::init()
{
    if (s_pads[0].thread != LWP_THREAD_NULL) return true;
    if (USB_Initialize() != IPC_OK) return false;
    if (s_heap < 0) s_heap = iosCreateHeap(4096);
    if (s_heap < 0) return false;
    if (s_lock == LWP_MUTEX_NULL)  LWP_MutexInit(&s_lock, false);
    if (s_claim == LWP_MUTEX_NULL) LWP_MutexInit(&s_claim, false);

    s_quit = false;
    int started = 0;
    for (Pad& p : s_pads) {
        if (!p.buf)   p.buf   = (u8*)iosAlloc(s_heap, MAX_REPORT);
        if (!p.cmd)   p.cmd   = (u8*)iosAlloc(s_heap, MAX_REPORT);
        if (!p.stack) p.stack = (u8*)memalign(32, STACK_SIZE);
        if (!p.buf || !p.cmd || !p.stack) break;
        /* the semaphores stay: a callback may still come after shutdown() */
        if (p.readSema == LWP_SEM_NULL) LWP_SemInit(&p.readSema, 0, 1);
        if (p.waitSema == LWP_SEM_NULL) LWP_SemInit(&p.waitSema, 0, 1);
        if (LWP_CreateThread(&p.thread, padMain, &p, p.stack, STACK_SIZE, 65) < 0) {
            p.thread = LWP_THREAD_NULL;
            break;
        }
        ++started;
    }
    return started > 0;
}

void UsbGamepad::shutdown()
{
    s_quit = true;
    for (Pad& p : s_pads) {
        if (p.thread == LWP_THREAD_NULL) continue;
        LWP_SemPost(p.readSema);
        LWP_SemPost(p.waitSema);
        LWP_JoinThread(p.thread, nullptr);
        p.thread = LWP_THREAD_NULL;
    }
}

/* Every pad together: the buttons of all, each stick from the pad pushing
 * it furthest */
UsbGamepad::State UsbGamepad::take()
{
    State st{};
    if (s_lock == LWP_MUTEX_NULL) return st;
    int best = 0, bestSub = 0;
    LWP_MutexLock(s_lock);
    for (Pad& p : s_pads) {
        if (!p.connected) continue;
        st.connected = true;
        st.pressed |= p.pressed;
        st.held    |= p.held;
        p.pressed = 0;
        const int m = p.sx * p.sx + p.sy * p.sy;
        if (m > best) { best = m; st.stickX = p.sx; st.stickY = p.sy; }
        const int ms = p.rx * p.rx + p.ry * p.ry;
        if (ms > bestSub) { bestSub = ms; st.subX = p.rx; st.subY = p.ry; }
    }
    LWP_MutexUnlock(s_lock);
    return st;
}

bool UsbGamepad::connected()
{
    for (const Pad& p : s_pads)
        if (p.connected) return true;
    return false;
}

bool UsbGamepad::isPlayStation()
{
    return connected() && (s_lastType == PadType::DualShock4 || s_lastType == PadType::DualShock3);
}

bool UsbGamepad::isXbox()
{
    return connected() && (s_lastType == PadType::XboxOne || s_lastType == PadType::Xbox360);
}

bool UsbGamepad::owns(s32 device)
{
    for (const Pad& p : s_pads)
        if (p.connected && p.device == device) return true;
    return false;
}
