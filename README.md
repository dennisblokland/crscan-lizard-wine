# crscan-lizard-wine

Use a **Creality CR-Scan Lizard** 3D scanner on **Linux** by running the official
**CRStudio** (Windows) under Wine.

Out of the box CRStudio starts under Wine but never connects to the scanner. This repo adds
three tiny replacement DLLs and a helper script; with them CRStudio detects the scanner as
"CR-Scan Lizard" and live preview, Brightness control and 3D scanning work.

> Not affiliated with or endorsed by Creality. No Creality software is included — you need
> your own copy of CRStudio. "Creality", "CR-Scan" and "CRStudio" are trademarks of their owners.

## Status

| | |
|---|---|
| Scanner detection, serial number | ✅ |
| Live camera preview + live 3D | ✅ |
| Brightness slider | ✅ (via `lizard_ctrl_bridge.py`) |
| Sensitivity slider | ✅ (it's a reconstruction setting, see below) |
| Meshing (Process) | ✅ with Microsoft's `vcomp140` (set by `run.sh`) |
| Tested with | CRStudio v2.5.14 (Windows build), Wine 11.19, Arch/CachyOS, kernel 7.2 |
| Other Creality scanners | untested; CR-Scan 01 uses a serial port and other cameras, not covered |

## Requirements

* Wine (tested 11.19)
* `v4l-utils` — Wine's camera capture uses libv4l2 to decode the scanner's MJPG; also provides `v4l2-ctl`
* `clang`, `lld` (`lld-link`), `llvm` (`llvm-dlltool`) — to build the shims, no MinGW needed
* Python 3 (standard library only)
* The Lizard plugged in. It shows up as two standard webcams, `ICT CameraA` and `ICT CameraB`
  (`lsusb`: `bcdc:000a`, `bcdc:000b`); no kernel driver needed.
  Your user must be able to open `/dev/video*` (usually the `video` group).

## Install

```sh
git clone https://github.com/dennisblokland/crscan-lizard-wine
cd crscan-lizard-wine

# CRStudio is modified in place — work on a copy of the Windows install folder
cp -a /path/to/CRStudio ~/CRStudio-wine

./install.sh ~/CRStudio-wine            # or: ./install.sh ~/CRStudio-wine /dev/videoN
./run.sh ~/CRStudio-wine
```

`install.sh`:

1. builds the three shims and installs them (originals kept as `*.orig`),
2. reads the scanner's serial-number flash into `lizard_flash.bin` (read-only),
3. downloads the factory calibration `calib.txt` for that serial number from Creality's
   server — the same request CRStudio makes on first connect. Skipped if `calib.txt` exists.

`run.sh` starts CRStudio in its own Wine prefix (`~/.local/share/crscan-lizard-wine/prefix`,
override with `WINEPREFIX`) together with the camera-settings bridge, and makes Wine use the
OpenMP runtime (`vcomp140.dll`) shipped with CRStudio — with Wine's builtin one the mesher
loses most of the surface. Logs:
`crstudio.log` and `ctrl_bridge.log` in the CRStudio folder.

## Usage notes

* Working distance is short: hold the object roughly **10–30 cm** from the scanner.
  The distance gauge on the left shows "Too Far" / "Too Close".
* **Brightness** changes exposure and gain of the pattern camera (CameraA).
* **Sensitivity** is not a camera setting: it is the smoothing parameter of CRStudio's
  depth decoder (higher = more detail, good for dark objects; lower = less noise). It is
  read when a scan starts, so set it before pressing Scan. CRStudio does not save it.
* If the processed mesh shows webs/flaps, turn off **Repair** (hole filling) when processing,
  or remove stray points first: the hole filler also bridges the edges of leftover noise.

## How it works

| Shim | Problem under Wine | What it does |
|---|---|---|
| `shims/hcamera` → `HCamera_x64.dll` | The original reads the scanner SN from flash through DirectShow `IKsControl` on the UVC extension unit. Wine doesn't implement that, so the scanner is never identified. | Serves `hEnumVideoDevice` / `hFreeVideoDevice` / `hRclReadFlash` from `lizard_flash.bin`. |
| `shims/qtmm` → `Qt5Multimedix.dll` | `uvcCameraPlugin.dll` finds the cameras by `vid_bcdc&pid_000a/000b` in the DirectShow device path; Wine reports `dummy_videoN`. | Proxies the plugin's five Qt5Multimedia imports and appends the real USB IDs to `QCameraInfo::deviceName()` for `ICT CameraA/B/C`. |
| `shims/cvcap` → `opencv_highgux2413.dll` | Brightness is applied with `cv::VideoCapture::set` (exposure/gain); Wine's DirectShow capture doesn't pass these to V4L2. | Wraps the plugin's `VideoCapture` (`open`/`set`/`get`) and sends each setting over localhost UDP (port 47811) to `tools/lizard_ctrl_bridge.py`, which applies it with `v4l2-ctl`. |

`install.sh` points `uvcCameraPlugin.dll` at the two proxies by renaming two import names
(`Qt5Multimedia.dll` → `Qt5Multimedix.dll`, `opencv_highgui2413.dll` → `opencv_highgux2413.dll`;
same length, nothing else in the binary changes).

The shims are freestanding C (no CRT), built with `clang --target=x86_64-pc-windows-msvc` and `lld-link`.

## Tools

* `tools/lizard_xu.py` — native Linux access to the Lizard's UVC extension unit
  (`UVCIOC_CTRL_QUERY`): serial number, flash, registers.
  `python3 tools/lizard_xu.py -d /dev/video2 info`
* `tools/lizard_calib_fetch.py SN --crstudio DIR -o calib.txt` — download the factory calibration by serial number
  (the request-signing key is read from your CRStudio install, it is not stored here).
* `tools/lizard_ctrl_bridge.py` — applies CRStudio camera settings to V4L2 (started by `run.sh`).

Reverse-engineering notes (XU protocol, calibration format, module map, Wine issues):
[docs/FINDINGS.md](docs/FINDINGS.md).

## Troubleshooting

| Log line in `crstudio.log` | Check |
|---|---|
| `Current device is Scan-Lite` + `SerialNumber is null` | `lizard_flash.bin` missing/empty in the CRStudio folder → re-run `install.sh` |
| `Camera ScannerType_Lite connected failed` | `Qt5Multimedix.dll` not next to `CRStudio.exe`, or `install.sh` not run on this copy |
| Brightness does nothing | `ctrl_bridge.log`: should list the capture nodes and `... [ok]` lines when you move the slider; "port busy" → another bridge is running |
| Mesh after Process is nearly empty (a few hundred points) | Wine's builtin `vcomp140` was used; start via `run.sh` (sets `WINEDLLOVERRIDES=vcomp140=n,b`). `idx=-1, the index is out of boundary` warnings in `meshrecon.log` are normal |
| No cameras at all | `v4l2-ctl --list-devices` must show `ICT CameraA/B`; install `v4l-utils` (libv4l2) |

Restore the original files with the `*.orig` copies (`HCamera_x64.dll.orig`,
`plugins_device/uvcCameraPlugin.dll.orig`).

## Privacy

`install.sh` / `lizard_calib_fetch.py` send your scanner's serial number to
`sw.3dyunzhan.com` (Creality's calibration service) to download its calibration, exactly as
CRStudio does. CRStudio itself also contacts Creality services while running. Nothing is sent anywhere else.

## License

MIT — see [LICENSE](LICENSE). Applies to the code in this repository only.
