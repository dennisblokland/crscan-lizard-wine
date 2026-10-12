#!/bin/sh
# Build Qt5Multimedix.dll proxy with clang + lld (no MinGW needed).
set -e
cd "$(dirname "$0")"
llvm-dlltool -m i386:x86-64 -d kernel32.def -l kernel32.lib
clang --target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -c qtmm_shim.c -o qtmm_shim.o
lld-link /dll /entry:DllMainCRTStartup /nodefaultlib /machine:x64 /out:Qt5Multimedix.dll \
    '/export:?description@QCameraInfo@@QEBA?AVQString@@XZ=shim_description' \
    '/export:??1QCameraInfo@@QEAA@XZ=shim_dtor' \
    '/export:?deviceName@QCameraInfo@@QEBA?AVQString@@XZ=shim_deviceName' \
    '/export:?availableCameras@QCameraInfo@@SA?AV?$QList@VQCameraInfo@@@@W4Position@QCamera@@@Z=shim_available' \
    '/export:??0QCameraInfo@@QEAA@AEBV0@@Z=shim_copy' \
    qtmm_shim.o kernel32.lib
