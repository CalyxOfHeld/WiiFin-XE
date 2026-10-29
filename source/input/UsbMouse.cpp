#include "UsbMouse.h"
#include "UsbGamepad.h"
#include <gccore.h>
#include <ogc/usb.h>
#include <ogc/ipc.h>
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/semaphore.h>
#include <ogc/system.h>
#include <string.h>
#include <unistd.h>

namespace {

const int MAX_DEVICES = 16;     /* HID interfaces looked at */
const int MAX_REPORT  = 64;     /* bytes of one report */
const int MAX_RDESC   = 1024;   /* bytes of a report descriptor */

/* Where a report keeps one value: bit offset, bit size, signed */
struct Field { int off = -1, size = 0; bool sign = false; };

/* How the mouse's reports read: the boot protocol (3-4 bytes: buttons, x,
 * y, maybe a wheel), or what its report descriptor says */
struct Layout {
    bool  boot = true;
    int   id = -1;            /* report id (-1: the reports carry none) */
    Field btn[3], x, y, wheel;
};

struct Mouse {
    bool   connected = false;
    s32    fd = -1;
    s32    device = -1;       /* its usb_device_entry id */
    u8     ep = 0;
    u16    len = 8;           /* bytes asked for at each read */
    bool   bootWheel = true;  /* boot: a 4th byte of -1..1 is a wheel, until it isn't */
    Layout lay;
};

s32   s_heap = -1;
u8*   s_buf = nullptr;                    /* 32-aligned, IOS heap */
Mouse s_m;
lwp_t s_thread = LWP_THREAD_NULL;
sem_t s_readSema = LWP_SEM_NULL;          /* a read finished, or the mouse went */
sem_t s_waitSema = LWP_SEM_NULL;          /* a device came or went (no mouse yet) */
mutex_t s_lock = LWP_MUTEX_NULL;
volatile bool s_quit = false;
volatile s32  s_result = 0;
u8 s_stack[16 * 1024] __attribute__((aligned(32)));

/* what take() hands out */
int s_dx = 0, s_dy = 0, s_wheel = 0;
u8  s_pressed = 0, s_held = 0;

s32 readCb(s32 result, void*)
{
    s_result = result;
    LWP_SemPost(s_readSema);
    return 0;
}

s32 removedCb(s32, void*)
{
    s_m.connected = false;
    LWP_SemPost(s_readSema);
    return 1;
}

s32 changeCb(s32, void*)
{
    LWP_SemPost(s_waitSema);
    return 1;
}

int bits(const u8* d, int len, const Field& f)
{
    if (f.off < 0 || f.size <= 0 || f.size > 32 || (f.off + f.size + 7) / 8 > len) return 0;
    u32 v = 0;
    for (int i = 0; i < f.size; ++i) {
        const int b = f.off + i;
        if ((d[b >> 3] >> (b & 7)) & 1) v |= 1u << i;
    }
    if (f.sign && f.size < 32 && ((v >> (f.size - 1)) & 1)) v |= ~0u << f.size;
    return (int)v;
}

/* The report descriptor: where the mouse application collection
 * (Generic Desktop / Mouse) keeps its buttons, X, Y and wheel.  Only
 * variable, non-constant input fields of one report id count. */
bool parseReport(const u8* d, int n, Layout& L)
{
    struct Globals { u32 page, size, count; s32 lmin; int id; };
    Globals g{ 0, 0, 0, 0, -1 }, saved[4];
    int sp = 0;
    u32 usages[16];
    int nUsage = 0;
    u32 umin = 0;
    bool range = false;
    int pos[256] = {};                    /* next input bit, per report id */
    int depth = 0, mouseDepth = -1;
    bool found = false;
    int foundId = -1;
    for (int i = 0; i < n; ) {
        const u8 p = d[i++];
        if (p == 0xFE) {                  /* long item: its size, its tag, its data */
            if (i >= n) break;
            i += 2 + d[i];
            continue;
        }
        const int sz = (p & 3) == 3 ? 4 : (p & 3);
        if (i + sz > n) break;
        u32 u = 0;
        for (int k = 0; k < sz; ++k) u |= (u32)d[i + k] << (8 * k);
        const s32 s = sz == 1 ? (s32)(s8)u : sz == 2 ? (s32)(s16)u : (s32)u;
        i += sz;
        const int type = (p >> 2) & 3, tag = p >> 4;
        if (type == 1) {                  /* global */
            switch (tag) {
            case 0:  g.page = u; break;
            case 1:  g.lmin = s; break;
            case 7:  g.size = u; break;
            case 8:  g.id = (int)(u & 0xFF); break;
            case 9:  g.count = u; break;
            case 10: if (sp < 4) saved[sp++] = g; break;
            case 11: if (sp > 0) g = saved[--sp]; break;
            }
        } else if (type == 2) {           /* local */
            const u32 full = sz == 4 ? u : (g.page << 16) | u;
            if (tag == 0 && nUsage < 16) usages[nUsage++] = full;
            else if (tag == 1) { umin = full; range = true; }
        } else if (type == 0) {           /* main */
            if (tag == 10) {              /* collection */
                if (mouseDepth < 0 && u == 1 && nUsage > 0 && usages[0] == 0x00010002u) mouseDepth = depth;
                ++depth;
            } else if (tag == 12) {       /* end of collection */
                --depth;
                if (depth == mouseDepth) {
                    mouseDepth = -1;
                    if (found) break;
                }
            } else if (tag == 8) {        /* input */
                int& bp = pos[g.id < 0 ? 0 : g.id];
                const bool constant = u & 1, variable = u & 2;
                for (u32 k = 0; k < g.count && k < 64; ++k) {
                    if (mouseDepth >= 0 && !constant && variable && (!found || g.id == foundId)) {
                        const u32 usage = range ? umin + k : nUsage ? usages[(int)k < nUsage ? k : nUsage - 1] : 0;
                        const u32 page = usage >> 16, id = usage & 0xFFFF;
                        const Field f{ bp, (int)g.size, g.lmin < 0 };
                        bool used = true;
                        if (page == 9 && id >= 1 && id <= 3) L.btn[id - 1] = f;
                        else if (page == 1 && id == 0x30)    L.x = f;
                        else if (page == 1 && id == 0x31)    L.y = f;
                        else if (page == 1 && id == 0x38)    L.wheel = f;
                        else used = false;
                        if (used) { found = true; foundId = g.id; }
                    }
                    bp += g.size;
                }
            }
            nUsage = 0;                   /* locals end with each main item */
            range = false;
        }
    }
    L.boot = false;
    L.id = foundId;
    return L.x.off >= 0 && L.y.off >= 0;
}

void closeMouse()
{
    if (s_m.fd >= 0) USB_CloseDevice(&s_m.fd);
    s_m.fd = -1;
    s_m.connected = false;
}

/* An interface's first interrupt IN endpoint */
const usb_endpointdesc* inEndpoint(const usb_interfacedesc* id)
{
    for (int e = 0; e < id->bNumEndpoints; ++e) {
        const usb_endpointdesc* ed = &id->endpoints[e];
        if ((ed->bmAttributes & 3) == USB_ENDPOINT_INTERRUPT && (ed->bEndpointAddress & USB_ENDPOINT_IN))
            return ed;
    }
    return nullptr;
}

struct Candidate { usb_device_entry dev; u8 iface, ep; u16 epSize; int rank; };

/* Opens one candidate: a boot mouse in the boot protocol, any other
 * through its report descriptor */
bool openCandidate(const Candidate& c)
{
    s32 fd = -1;
    if (USB_OpenDevice(c.dev.device_id, c.dev.vid, c.dev.pid, &fd) < 0) {
        SYS_Report("[Mouse] %04x:%04x: could not open\n", c.dev.vid, c.dev.pid);
        return false;
    }
    Layout lay;
    if (c.rank > 0) {
        static u8 rdesc[MAX_RDESC];
        const s32 size = USB_GetReportDescriptorSize(fd, c.iface);
        if (size <= 0 || size > MAX_RDESC || USB_GetReportDescriptor(fd, c.iface, rdesc, (u16)size) < 0) {
            SYS_Report("[Mouse] %04x:%04x interface %d: no report descriptor (%d)\n",
                       c.dev.vid, c.dev.pid, c.iface, (int)size);
            USB_CloseDevice(&fd);
            return false;
        }
        if (!parseReport(rdesc, size, lay)) {
            SYS_Report("[Mouse] %04x:%04x interface %d: not a mouse (%d byte descriptor)\n",
                       c.dev.vid, c.dev.pid, c.iface, (int)size);
            USB_CloseDevice(&fd);
            return false;
        }
        SYS_Report("[Mouse] report id %d: buttons @%d, x @%d/%d, y @%d/%d, wheel @%d/%d\n", lay.id,
                   lay.btn[0].off, lay.x.off, lay.x.size, lay.y.off, lay.y.size, lay.wheel.off, lay.wheel.size);
    }
    s_m = Mouse();
    s_m.fd = fd;
    s_m.device = c.dev.device_id;
    s_m.ep = c.ep;
    s_m.len = c.epSize < 8 ? 8 : c.epSize > MAX_REPORT ? MAX_REPORT : c.epSize;
    s_m.lay = lay;
    if (USB_DeviceRemovalNotifyAsync(fd, removedCb, nullptr) < 0) {
        closeMouse();
        return false;
    }
    if (lay.boot)   /* the boot protocol: SET_PROTOCOL 0 */
        USB_WriteCtrlMsg(fd, USB_REQTYPE_INTERFACE_SET, USB_REQ_SETPROTOCOL, 0, c.iface, 0, nullptr);
    s_m.connected = true;
    SYS_Report("[Mouse] %04x:%04x interface %d: %s, endpoint 0x%02x, %d bytes\n", c.dev.vid, c.dev.pid,
               c.iface, lay.boot ? "boot mouse" : "HID mouse", c.ep, s_m.len);
    return true;
}

/* Looks through the HID interfaces (IOS shows each interface of a device
 * as a device of its own): boot mice first, then other mice, then HID
 * interfaces that may be one (their report descriptor tells).  Keyboards
 * are left to the keyboard driver. */
bool findMouse()
{
    static usb_device_entry list[MAX_DEVICES] __attribute__((aligned(32)));
    u8 count = 0;
    memset(list, 0, sizeof(list));
    if (USB_GetDeviceList(list, MAX_DEVICES, USB_CLASS_HID, &count) < 0) return false;
    Candidate cands[MAX_DEVICES];
    int nc = 0;
    for (int i = 0; i < count && i < MAX_DEVICES; ++i) {
        const usb_device_entry& e = list[i];
        if (!e.vid || !e.pid) continue;
        /* the gamepad driver's: closing it after a look would fire the
         * gamepad's removal callback */
        if (UsbGamepad::owns(e.device_id)) continue;
        /* read without opening it (an opened device closed is suspended,
         * and the keyboard driver may be using it); the old USB interface
         * (no IOS 58) needs it opened */
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
        if (got < 0) {
            SYS_Report("[Mouse] %04x:%04x: no descriptors\n", e.vid, e.pid);
            continue;
        }
        for (int c = 0; c < dd.bNumConfigurations; ++c) {
            const usb_configurationdesc& cd = dd.configurations[c];
            for (int f = 0; f < cd.bNumInterfaces && cd.interfaces; ++f) {
                const usb_interfacedesc* id = &cd.interfaces[f];
                const usb_endpointdesc* ed = inEndpoint(id);
                SYS_Report("[Mouse] HID %04x:%04x interface %d: subclass %d protocol %d, endpoint 0x%02x %d bytes\n",
                           e.vid, e.pid, id->bInterfaceNumber, id->bInterfaceSubClass, id->bInterfaceProtocol,
                           ed ? ed->bEndpointAddress : 0, ed ? ed->wMaxPacketSize : 0);
                if (id->bInterfaceClass != USB_CLASS_HID || !ed || nc >= MAX_DEVICES) continue;
                int rank;
                if (id->bInterfaceProtocol == USB_PROTOCOL_MOUSE)
                    rank = id->bInterfaceSubClass == USB_SUBCLASS_BOOT ? 0 : 1;
                else if (id->bInterfaceProtocol == 0)
                    rank = 2;
                else
                    continue;                     /* a keyboard */
                cands[nc++] = { e, id->bInterfaceNumber, ed->bEndpointAddress, ed->wMaxPacketSize, rank };
            }
        }
        USB_FreeDescriptors(&dd);
    }
    for (int rank = 0; rank <= 2; ++rank)
        for (int i = 0; i < nc; ++i)
            if (cands[i].rank == rank && openCandidate(cands[i])) return true;
    return false;
}

void decode(const u8* d, int len)
{
    int dx = 0, dy = 0, wheel = 0;
    u8 btn = 0;
    const Layout& L = s_m.lay;
    if (L.boot) {
        if (len < 3) return;
        btn = d[0] & 7;
        dx = (s8)d[1];
        dy = (s8)d[2];
        if (s_m.bootWheel) {
            if (len < 4 || (s8)d[3] < -1 || (s8)d[3] > 1) s_m.bootWheel = false;
            else wheel = (s8)d[3];
        }
    } else {
        if (L.id >= 0) {
            if (len < 1 || d[0] != L.id) return;  /* another report (keys, consumer...) */
            ++d;
            --len;
        }
        for (int i = 0; i < 3; ++i)
            if (bits(d, len, L.btn[i])) btn |= 1 << i;
        dx = bits(d, len, L.x);
        dy = bits(d, len, L.y);
        wheel = bits(d, len, L.wheel);
    }
    LWP_MutexLock(s_lock);
    s_dx += dx;
    s_dy += dy;
    s_wheel += wheel;
    s_pressed |= btn & ~s_held;
    s_held = btn;
    LWP_MutexUnlock(s_lock);
}

void* threadMain(void*)
{
    while (!s_quit) {
        if (!s_m.connected) {
            closeMouse();
            LWP_MutexLock(s_lock);
            s_held = 0;
            LWP_MutexUnlock(s_lock);
            if (!findMouse()) {
                /* wait for something to be plugged in */
                USB_DeviceChangeNotifyAsync(USB_CLASS_HID, changeCb, nullptr);
                LWP_SemWait(s_waitSema);
                continue;
            }
        }
        s_result = 0;
        if (USB_ReadIntrMsgAsync(s_m.fd, s_m.ep, s_m.len, s_buf, readCb, nullptr) < 0) {
            SYS_Report("[Mouse] read could not start\n");
            s_m.connected = false;
            usleep(200000);
            continue;
        }
        LWP_SemWait(s_readSema);
        if (s_quit) break;
        if (s_result > 0) {
            decode(s_buf, s_result);
        } else if (s_result < 0) {
            SYS_Report("[Mouse] gone (%d)\n", (int)s_result);
            s_m.connected = false;
        }
    }
    return nullptr;
}

} // namespace

bool UsbMouse::init()
{
    if (s_thread != LWP_THREAD_NULL) return true;
    if (USB_Initialize() != IPC_OK) return false;
    if (s_heap < 0) s_heap = iosCreateHeap(1024);
    if (s_heap < 0) return false;
    if (!s_buf) s_buf = (u8*)iosAlloc(s_heap, MAX_REPORT);
    if (!s_buf) return false;
    /* the semaphores stay: a callback may still come after shutdown() */
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

void UsbMouse::shutdown()
{
    if (s_thread == LWP_THREAD_NULL) return;
    s_quit = true;
    LWP_SemPost(s_waitSema);
    LWP_SemPost(s_readSema);
    LWP_JoinThread(s_thread, nullptr);
    s_thread = LWP_THREAD_NULL;
    closeMouse();
}

bool UsbMouse::connected() { return s_thread != LWP_THREAD_NULL && s_m.connected; }

s32 UsbMouse::deviceId() { return s_m.connected ? s_m.device : -1; }

UsbMouse::State UsbMouse::take()
{
    State st{ 0, 0, 0, 0, 0 };
    if (s_lock == LWP_MUTEX_NULL) return st;
    LWP_MutexLock(s_lock);
    st = { s_dx, s_dy, s_wheel, s_pressed, s_held };
    s_dx = s_dy = s_wheel = 0;
    s_pressed = 0;
    LWP_MutexUnlock(s_lock);
    return st;
}
