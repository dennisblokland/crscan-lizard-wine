/*
 * Drop-in replacement for HCamera_x64.dll (CRStudio) for running under Wine.
 *
 * The original talks to the scanner's UVC extension unit through DirectShow
 * IKsControl, which Wine does not implement. scanCore.dll only imports
 * hEnumVideoDevice / hFreeVideoDevice / hRclReadFlash, and only to read the
 * serial number at flash offset 0xFE00. This shim serves those reads from a
 * flash image dumped on the Linux side (linux/tools/lizard_xu.py), stored next
 * to CRStudio.exe as "lizard_flash.bin" (file offset == flash address).
 */
#include <stdint.h>

typedef void *HANDLE;
typedef uint32_t DWORD;
typedef int BOOL;

__declspec(dllimport) HANDLE __stdcall CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE, void *, DWORD, DWORD *, void *);
__declspec(dllimport) DWORD __stdcall SetFilePointer(HANDLE, long, long *, DWORD);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) DWORD __stdcall GetModuleFileNameA(HANDLE, char *, DWORD);

#define GENERIC_READ 0x80000000u
#define FILE_SHARE_READ 1u
#define OPEN_EXISTING 3u
#define INVALID_HANDLE ((HANDLE)(intptr_t)-1)

static HANDLE self_module;

static HANDLE open_image(void)
{
    char path[1024];
    DWORD n = GetModuleFileNameA(self_module, path, sizeof(path) - 32);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        n--;
    const char *name = "lizard_flash.bin";
    for (int i = 0; name[i]; i++)
        path[n++] = name[i];
    path[n] = 0;
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
}

/*
 * Device count. scanCore addresses devices by their DirectShow index, which
 * includes unrelated webcams, so every index up to MAX_DEVICES is answered
 * from the same flash image.
 */
#define MAX_DEVICES 16

__declspec(dllexport) int hEnumVideoDevice(void)
{
    HANDLE h = open_image();
    if (h == INVALID_HANDLE)
        return 0;
    CloseHandle(h);
    return MAX_DEVICES;
}

__declspec(dllexport) void hFreeVideoDevice(void) {}

__declspec(dllexport) int hRclReadFlash(int dev, uint32_t addr, uint8_t *buf, int len)
{
    if (dev < 0 || dev >= MAX_DEVICES || len <= 0)
        return 0;
    HANDLE h = open_image();
    if (h == INVALID_HANDLE)
        return 0;
    DWORD got = 0;
    BOOL ok = SetFilePointer(h, (long)addr, 0, 0) != 0xFFFFFFFFu &&
              ReadFile(h, buf, (DWORD)len, &got, 0) && got == (DWORD)len;
    CloseHandle(h);
    return ok ? 1 : 0;
}

BOOL __stdcall DllMainCRTStartup(HANDLE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == 1)
        self_module = inst;
    return 1;
}
