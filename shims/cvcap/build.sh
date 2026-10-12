#!/bin/sh
# Build opencv_highgux2413.dll proxy with clang + lld (no MinGW needed).
set -e
cd "$(dirname "$0")"
llvm-dlltool -m i386:x86-64 -d kernel32.def -l kernel32.lib
llvm-dlltool -m i386:x86-64 -d ws2_32.def -l ws2_32.lib
clang --target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -fno-builtin -c cvcap_shim.c -o cvcap_shim.o
lld-link /dll /entry:DllMainCRTStartup /nodefaultlib /machine:x64 /out:opencv_highgux2413.dll \
    '/export:??0VideoCapture@cv@@QEAA@XZ=shim_ctor' \
    '/export:??1VideoCapture@cv@@UEAA@XZ=shim_dtor' \
    cvcap_shim.o kernel32.lib ws2_32.lib
