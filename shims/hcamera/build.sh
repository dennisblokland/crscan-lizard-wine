#!/bin/sh
# Build HCamera_x64.dll shim with clang + lld (no MinGW needed).
set -e
cd "$(dirname "$0")"
llvm-dlltool -m i386:x86-64 -d kernel32.def -l kernel32.lib
clang --target=x86_64-pc-windows-msvc -O2 -ffreestanding -fno-stack-protector -c hcamera_shim.c -o hcamera_shim.o
lld-link /dll /entry:DllMainCRTStartup /nodefaultlib /machine:x64 \
    /out:HCamera_x64.dll hcamera_shim.o kernel32.lib
