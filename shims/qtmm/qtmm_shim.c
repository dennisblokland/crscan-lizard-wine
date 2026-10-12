/*
 * Qt5Multimedix.dll — proxy for the five Qt5Multimedia imports of
 * uvcCameraPlugin.dll (CRStudio), for running under Wine.
 *
 * uvcCameraPlugin identifies the scanner cameras by parsing
 * "vid_XXXX&pid_YYYY&mi_" out of QCameraInfo::deviceName(), i.e. the
 * DirectShow device path. Wine's devenum reports "dummy_videoN" instead, so
 * the cameras are never matched. deviceName() is forwarded and, for the
 * Lizard's "ICT CameraA/B/C" devices, suffixed with the real USB IDs.
 * Everything else is forwarded unchanged to the real Qt5Multimedia.dll.
 */
#include <stdint.h>

typedef void *HMODULE;
__declspec(dllimport) HMODULE __stdcall LoadLibraryA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(HMODULE, const char *);

typedef void *(*fwd4)(void *, void *, void *, void *);
typedef struct { void *d; } QString;   /* QString is a single d-pointer */

static fwd4 r_description, r_dtor, r_deviceName, r_available, r_copy;
static void *(*q_fromAscii)(const char *, int);
static void *(*q_append)(QString *, const QString *);
static void (*q_dtor)(QString *);

static void init(void)
{
    HMODULE mm = LoadLibraryA("Qt5Multimedia.dll");
    HMODULE core = LoadLibraryA("Qt5Core.dll");
    r_description = (fwd4)GetProcAddress(mm, "?description@QCameraInfo@@QEBA?AVQString@@XZ");
    r_dtor = (fwd4)GetProcAddress(mm, "??1QCameraInfo@@QEAA@XZ");
    r_deviceName = (fwd4)GetProcAddress(mm, "?deviceName@QCameraInfo@@QEBA?AVQString@@XZ");
    r_available = (fwd4)GetProcAddress(mm, "?availableCameras@QCameraInfo@@SA?AV?$QList@VQCameraInfo@@@@W4Position@QCamera@@@Z");
    r_copy = (fwd4)GetProcAddress(mm, "??0QCameraInfo@@QEAA@AEBV0@@Z");
    q_fromAscii = (void *(*)(const char *, int))GetProcAddress(core, "?fromAscii_helper@QString@@CAPEAU?$QTypedArrayData@G@@PEBDH@Z");
    q_append = (void *(*)(QString *, const QString *))GetProcAddress(core, "?append@QString@@QEAAAEAV1@AEBV1@@Z");
    q_dtor = (void (*)(QString *))GetProcAddress(core, "??1QString@@QEAA@XZ");
}

/* QString::data of a Qt5 QArrayData: utf16 payload at d + offset */
static const uint16_t *qstr_utf16(const QString *s, int *len)
{
    const uint8_t *d = s->d;
    *len = *(const int *)(d + 4);
    return (const uint16_t *)(d + *(const int64_t *)(d + 16));
}

static int starts_with(const QString *s, const char *p)
{
    int len;
    const uint16_t *u = qstr_utf16(s, &len);
    for (int i = 0; p[i]; i++)
        if (i >= len || u[i] != (uint16_t)p[i])
            return 0;
    return 1;
}

void *shim_description(void *a, void *b, void *c, void *d) { return r_description(a, b, c, d); }
void *shim_dtor(void *a, void *b, void *c, void *d) { return r_dtor(a, b, c, d); }
void *shim_available(void *a, void *b, void *c, void *d) { return r_available(a, b, c, d); }
void *shim_copy(void *a, void *b, void *c, void *d) { return r_copy(a, b, c, d); }

/* QString QCameraInfo::deviceName() const  ->  (this, QString *ret) */
void *shim_deviceName(void *self, QString *ret, void *c, void *d)
{
    r_deviceName(self, ret, c, d);
    QString desc;
    r_description(self, &desc, 0, 0);
    const char *suffix = 0;
    if (starts_with(&desc, "ICT CameraA"))
        suffix = "#vid_bcdc&pid_000a&mi_00#";
    else if (starts_with(&desc, "ICT CameraB"))
        suffix = "#vid_bcdc&pid_000b&mi_00#";
    else if (starts_with(&desc, "ICT CameraC"))
        suffix = "#vid_bcdc&pid_000c&mi_00#";
    if (suffix) {
        QString s;
        int n = 0;
        while (suffix[n])
            n++;
        s.d = q_fromAscii(suffix, n);
        q_append(ret, &s);
        q_dtor(&s);
    }
    q_dtor(&desc);
    return ret;
}

int __stdcall DllMainCRTStartup(void *inst, uint32_t reason, void *reserved)
{
    (void)inst; (void)reserved;
    if (reason == 1)
        init();
    return 1;
}
