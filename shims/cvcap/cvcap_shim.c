/*
 * opencv_highgux2413.dll — proxy for the VideoCapture imports of
 * uvcCameraPlugin.dll (CRStudio), for running under Wine.
 *
 * The plugin applies the Brightness/Sensitivity settings with
 * cv::VideoCapture::set (exposure, gain, ...). OpenCV forwards these to
 * DirectShow IAMCameraControl / IAMVideoProcAmp, which Wine's qcap does not
 * pass through to V4L2, so the sliders have no effect.
 *
 * The proxy constructs the real VideoCapture, then swaps in a copy of its
 * vtable with open(int)/set/get wrapped. Every set() is still forwarded to
 * OpenCV and is additionally sent as a UDP datagram
 *     "<dshow-index> <prop> <value>\n"
 * to 127.0.0.1:BRIDGE_PORT, where lizard_ctrl_bridge.py applies it with V4L2.
 * get() returns the last value set for that property, so the app reads back
 * what it asked for.
 */
#include <stdint.h>

typedef void *HMODULE;
__declspec(dllimport) HMODULE __stdcall LoadLibraryA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(HMODULE, const char *);

typedef uintptr_t SOCKET;
struct sockaddr_in { short family; uint16_t port; uint32_t addr; char zero[8]; };
__declspec(dllimport) int __stdcall WSAStartup(uint16_t, void *);
__declspec(dllimport) SOCKET __stdcall socket(int, int, int);
__declspec(dllimport) int __stdcall sendto(SOCKET, const char *, int, int, const void *, int);

int _fltused;  /* MSVC float-support marker, normally from the CRT */

#define BRIDGE_PORT 47811
#define VT_SLOTS 11
#define SLOT_OPEN_INDEX 1
#define SLOT_SET 9
#define SLOT_GET 10
#define MAX_CAPS 8
#define MAX_PROPS 64

typedef char (*open_fn)(void *, int);
typedef char (*set_fn)(void *, int, double);
typedef double (*get_fn)(void *, int);

static void (*r_ctor)(void *);
static void (*r_dtor)(void *);
static void **real_vt;
static void *patched_vt[VT_SLOTS];
static SOCKET sock = (SOCKET)-1;

static struct cap {
    void *obj;
    int index;
    uint64_t have;
    double val[MAX_PROPS];
} caps[MAX_CAPS];

static struct cap *find(void *obj, int create)
{
    for (int i = 0; i < MAX_CAPS; i++)
        if (caps[i].obj == obj)
            return &caps[i];
    if (!create)
        return 0;
    for (int i = 0; i < MAX_CAPS; i++)
        if (!caps[i].obj) {
            caps[i].obj = obj;
            caps[i].index = -1;
            caps[i].have = 0;
            return &caps[i];
        }
    return 0;
}

static int fmt_int(char *p, int v)
{
    char t[12];
    int n = 0, len = 0;
    unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) p[len++] = '-';
    while (n) p[len++] = t[--n];
    return len;
}

static void send_ctrl(int index, int prop, double value)
{
    if (sock == (SOCKET)-1 || index < 0)
        return;
    /* value as fixed point x1000: avoids pulling in a printf */
    char buf[48];
    int n = fmt_int(buf, index);
    buf[n++] = ' ';
    n += fmt_int(buf + n, prop);
    buf[n++] = ' ';
    n += fmt_int(buf + n, (int)(value * 1000.0 + (value < 0 ? -0.5 : 0.5)));
    buf[n++] = '\n';
    struct sockaddr_in to = {2 /* AF_INET */, (uint16_t)((BRIDGE_PORT >> 8) | ((BRIDGE_PORT & 0xff) << 8)),
                             0x0100007f /* 127.0.0.1 */, {0}};
    sendto(sock, buf, n, 0, &to, sizeof(to));
}

static char w_open(void *self, int index)
{
    struct cap *c = find(self, 1);
    if (c)
        c->index = index;
    return ((open_fn)real_vt[SLOT_OPEN_INDEX])(self, index);
}

static char w_set(void *self, int prop, double value)
{
    char ok = ((set_fn)real_vt[SLOT_SET])(self, prop, value);
    struct cap *c = find(self, 0);
    if (c && prop >= 0 && prop < MAX_PROPS) {
        c->val[prop] = value;
        c->have |= 1ull << prop;
        send_ctrl(c->index, prop, value);
        return 1;
    }
    return ok;
}

static double w_get(void *self, int prop)
{
    struct cap *c = find(self, 0);
    if (c && prop >= 0 && prop < MAX_PROPS && (c->have >> prop & 1))
        return c->val[prop];
    return ((get_fn)real_vt[SLOT_GET])(self, prop);
}

/* cv::VideoCapture::VideoCapture() */
void *shim_ctor(void *self)
{
    r_ctor(self);
    if (!real_vt) {
        real_vt = *(void ***)self;
        for (int i = 0; i < VT_SLOTS; i++)
            patched_vt[i] = real_vt[i];
        patched_vt[SLOT_OPEN_INDEX] = (void *)w_open;
        patched_vt[SLOT_SET] = (void *)w_set;
        patched_vt[SLOT_GET] = (void *)w_get;
    }
    *(void ***)self = patched_vt;
    find(self, 1);
    return self;
}

/* cv::VideoCapture::~VideoCapture() */
void shim_dtor(void *self)
{
    struct cap *c = find(self, 0);
    if (c)
        c->obj = 0;
    r_dtor(self);
}

int __stdcall DllMainCRTStartup(void *inst, uint32_t reason, void *reserved)
{
    (void)inst; (void)reserved;
    if (reason == 1) {
        HMODULE cv = LoadLibraryA("opencv_highgui2413.dll");
        r_ctor = (void (*)(void *))GetProcAddress(cv, "??0VideoCapture@cv@@QEAA@XZ");
        r_dtor = (void (*)(void *))GetProcAddress(cv, "??1VideoCapture@cv@@UEAA@XZ");
        char wsa[512];
        if (WSAStartup(0x0202, wsa) == 0)
            sock = socket(2 /* AF_INET */, 2 /* SOCK_DGRAM */, 17 /* UDP */);
    }
    return 1;
}
