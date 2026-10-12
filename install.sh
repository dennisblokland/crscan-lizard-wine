#!/bin/sh
# Prepare a CRStudio (Windows) install for running the CR-Scan Lizard under Wine.
#
#   ./install.sh /path/to/CRStudio [/dev/videoN]
#
# The CRStudio directory is modified in place — use a copy, not your only one.
#  - HCamera_x64.dll   replaced by shim (SN served from lizard_flash.bin)
#  - Qt5Multimedix.dll, opencv_highgux2413.dll added; uvcCameraPlugin.dll imports renamed to them
#  - lizard_flash.bin  dumped from the scanner's CameraA (default /dev/video2)
#  - calib.txt         downloaded by SN if missing
set -e
REPO="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "${1:?usage: install.sh /path/to/CRStudio [/dev/videoN]}" && pwd)"
DEV="${2:-}"
[ -f "$APP/CRStudio.exe" ] || { echo "no CRStudio.exe in $APP" >&2; exit 1; }

if [ -z "$DEV" ]; then
    for n in /sys/class/video4linux/video*; do
        if grep -qx "ICT CameraA: ICT CameraA" "$n/name" 2>/dev/null && [ "$(cat "$n/index")" = 0 ]; then
            DEV="/dev/$(basename "$n")"
            break
        fi
    done
fi
[ -n "$DEV" ] || { echo "Lizard CameraA not found; pass /dev/videoN" >&2; exit 1; }
echo "CameraA: $DEV"

"$REPO/shims/hcamera/build.sh"
"$REPO/shims/qtmm/build.sh"
"$REPO/shims/cvcap/build.sh"

# copy-then-rename so a running CRStudio keeps its mapped old files intact
put() { cp "$1" "$2.new" && mv -f "$2.new" "$2"; }
[ -f "$APP/HCamera_x64.dll.orig" ] || cp "$APP/HCamera_x64.dll" "$APP/HCamera_x64.dll.orig"
put "$REPO/shims/hcamera/HCamera_x64.dll" "$APP/HCamera_x64.dll"
put "$REPO/shims/qtmm/Qt5Multimedix.dll" "$APP/Qt5Multimedix.dll"
put "$REPO/shims/cvcap/opencv_highgux2413.dll" "$APP/opencv_highgux2413.dll"

python3 -I - "$APP/plugins_device/uvcCameraPlugin.dll" <<'PY'
import os, sys
p = sys.argv[1]
b = open(p, "rb").read()
if not os.path.exists(p + ".orig"):
    open(p + ".orig", "wb").write(b)
for old, new in ((b"Qt5Multimedia.dll", b"Qt5Multimedix.dll"),
                 (b"opencv_highgui2413.dll", b"opencv_highgux2413.dll")):
    if new not in b:
        assert b.count(old) == 1, "unexpected uvcCameraPlugin.dll"
        b = b.replace(old, new)
open(p + ".new", "wb").write(b)
os.replace(p + ".new", p)
PY

python3 -I "$REPO/tools/lizard_xu.py" -d "$DEV" flash 0x0 0x10000 -o "$APP/lizard_flash.bin"
SN="$(python3 -I "$REPO/tools/lizard_xu.py" -d "$DEV" info | sed -n 's/^SN: //p')"
echo "SN: $SN"
if [ ! -f "$APP/calib.txt" ]; then
    python3 -I "$REPO/tools/lizard_calib_fetch.py" "$SN" --crstudio "$APP" -o "$APP/calib.txt"
fi
echo "done — start with: $REPO/run.sh $APP"
