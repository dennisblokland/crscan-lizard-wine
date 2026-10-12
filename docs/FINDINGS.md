# CR-Scan Lizard — reverse-engineering notes (CRStudio 2.5.x for Windows, tested v2.5.14)

Device under test: one CR-Scan Lizard (SN prefix `CRSL`).

## Hardware as seen by Linux

| USB ID | Name | Role in CRStudio | Linux node (example) |
|---|---|---|---|
| `1a40:0101` | hub | internal hub | — |
| `bcdc:000a` | ICT CameraA | "code" camera, holds SN in flash | `/dev/video2` |
| `bcdc:000b` | ICT CameraB | "text" camera | `/dev/video4` |

* Both are plain **UVC** (Realtek "REALSIL" bridge), USB 2.0, mono sensor.
  `uvcvideo` binds them out of the box — **no kernel driver needed**.
* Formats: MJPG 640x480 / 1280x720 @10 fps, YUYV 640x480 @5 fps. CRStudio uses **MJPG 1280x720**.
* No serial port. The CP210x serial path in `scanCore.dll` (`Z%03dZ\r\n`, `CMD_LIGHT_OPEN`)
  is only for ScannerType 0 (CR-Scan 01). The Lizard projector is not commanded by the host.
* Vendor XU: GUID `{1229a78c-47b4-4094-b0ce-db07386fb938}`, unit 4, selectors 10 (cmd) / 11 (data).

## Device type / camera matching (scanCore, observed under Wine)

* `ParseDeviceSN`: `CRSL` + 11 chars → DeviceType 2 (Lizard). SN read per camera with
  `GetDeviceSNByIndex(DirectShow index)` → `hRclReadFlash(index, 0xFE00, …, 0x14)`.
* ScannerType "Lite" path (`createUVCCamera`) needs exactly 2 cameras, matched by
  VID `0xbcdc` and PID `0x0a` (A) / `0x0b` (B), parsed by `uvcCameraPlugin` from
  `QCameraInfo::deviceName()` (`…vid_XXXX&pid_YYYY&mi_…`).

## Module map

```
CRStudio.exe ─┬─ session.dll ──┬─ algorithm.dll ─ rulermvs113 / meshrecon / TextureMapping
              │                ├─ rgbdfusion.dll  (OneShot stereo depth + RGBDFusion TSDF)
              │                └─ scanCore.dll
              └─ scanCore.dll ─┬─ HCamera_x64.dll (DirectShow + IKsControl XU → flash/regs)
                               ├─ SonixCamera.dll (other models: SN @0x1f100)
                               └─ plugins_device/uvcCameraPlugin.dll (OpenCV 2.4 VideoCapture/DShow)
plugins/cloudServicePlugin.dll — calibration download
core.dll — JMSettingsManager, CalibFileParser, CommonTools::ParseDeviceSN/TrimDeviceSN
```

## XU protocol (from `HCamera_x64.dll` hRcl*)

All commands: SET_CUR 8 bytes on selector 10, then 8-byte chunks on selector 11
(GET_CUR to read, SET_CUR to write).

| Function | Command bytes |
|---|---|
| ReadFlash(addr, n) | `02 C2 a0 a1 nL nH a2 00` |
| ReadReg16(r) | `00 CA rL rH 02 00 00 00` |
| WriteReg16(r) | `00 8A rL rH 02 00 00 00` + data `[hi lo]` |
| ReadMem(a, n≤8) | `00 C2 aL aH n 00 00 00` |
| WriteMem(a, n≤8) | `00 82 aL aH n 00 00 00` + data |

* SN: `ReadFlash(0xFE00, 0x14)`, SN = bytes 4..18, must start `JM` or `CR`
  (`DeviceHelper::GetUvcDeviceSN`). Verified on Linux: `CRSLxxxxxxx`.
* Linux implementation: `tools/lizard_xu.py` (uses `UVCIOC_CTRL_QUERY`, works unprivileged).

## Camera parameters (from `uvcCameraPlugin.dll`)

* `setFOURCC`: MJPG, 1280x720.
* "BrightLevel" 1–9 → table of (DirectShow exposure log2 s, gain). Exposure −11…−7
  (≈0.5–8 ms), gain 20–100 depending on `Core/WorkMode`. Contrast set to 40 on one model.
* Some models use DShow **Roll** as gain and **Zoom**/**Sharpness** as extra knobs
  (descriptor advertises Roll-absolute; Linux uvcvideo doesn't map Roll → needs XU or raw ctrl).

## Frame pairing (`scanCore!SyncImagesThread`)

CameraA queue = "code", CameraB queue = "text". Pair heads when
`|tB − tA| ≤ 60 ms`; otherwise drop the older one.

## Reconstruction

`rgbdfusion.dll` exports `OneShot(calibPath, 6 floats)`, `OneShot::process(Image, Image, …)`,
`RGBDFusion(CameraPinhole…)`. → **single-shot stereo** on a static projected pattern
(no phase-shift sequence), then TSDF fusion. Replicable with OpenCV (rectify + SGBM) + Open3D.

## Projector (observed on Linux)

The projector runs on its own whenever the cameras stream; the host never commands it
(zoom/pan/tilt have no effect). It projects **vertical stripes**, visible in **CameraA only**
and only on near objects (≈10–30 cm; seen with exposure_time_absolute=20, gain=60).
CameraB shows no pattern → texture/tracking camera.

## Calibration

Not in camera flash. Downloaded per-SN by `cloudServicePlugin`:

1. `POST https://sw.3dyunzhan.com/v2/swift/calibration/find`, body `{"sn": SN}`,
   headers `sn: SN`, `sign: md5hex(md5hex(SN + KEY))`.
   KEY = the 44-char base64 string stored after the API URL in `cloudServicePlugin.dll`.
2. `GET uploadUrl` (headers `sn: 1`, matching sign) → `<appdir>/calib.txt`.
3. Lizard (ScannerType 2): `SplitCabFile` → `calibnear.txt`, `calibfar.txt`.

SN is trimmed first (`TrimDeviceSN`): Lizard → last 7 chars (`CRSLxxxxxxx` → `xxxxxxx`).
Tool: `tools/lizard_calib_fetch.py`.

### calib.txt layout (interpretation from one unit; ✓ = certain)

```
1280 720                       ✓ image size
9.800000 5.200000              ? (5.2 ≈ stripe pitch in the stripe table)
138 130                        ?
3 -47                          ?
<15 floats>  CameraA: fx fy cx cy k1 k2 p1 p2 k3, 6 zeros (identity pose)
<15 floats>  CameraB: fx fy cx cy k1 k2 p1 p2 k3, rvec(3), T(3)   T≈(29.5,-0.1,-1.6) mm
<15 floats>  Projector: fx=2017 fy=2497 cx cy k1..k3, rvec(3), T(3) T≈(56.5,0,-0.4) mm
137                            stripe count
<i> <a> <b> ×137               b rises ≈5.2/stripe (2.0 … 709.0); a small (slope?) → per-stripe geometry
216                            code length
<6 ints in 1..4> ×216          every row is a rotation of (1 2 1 3 2 4) → stripe identification code
***DevID:<sn>***CalibrateDate:<date>***Type:<station>***SoftVersion:<ver>
```

## Adjust panel (CRStudio.exe `AdjustPage`)

* **Brightness** (1–9) → `JMSession::setCameraBrightness` → uvcCameraPlugin level table →
  `VideoCapture::set(CV_CAP_PROP_EXPOSURE, log2 s)` + `set(CV_CAP_PROP_GAIN, raw)` on CameraA.
* **Sensitivity** (2–10, default 5) → `JMSettingsManager::SetOneShotSmooth(value)`, read by
  `session.dll` when the `rgbdfusion::OneShot` depth decoder is created (scan start). Not a camera
  setting and not persisted; the `Camera/Sensitivity` ini key is unused for the Lizard.

## Running under Wine (what breaks and why)

| Symptom | Cause | Fix in this repo |
|---|---|---|
| "Current device is Scan-Lite", camera connect fails, SN null | `HCamera_x64.dll` reads flash via DirectShow `IKsControl` → XU; Wine has no `IKsControl` on capture filters | `shims/hcamera` |
| "Camera ScannerType_Lite connected failed" with both cameras enumerated | Wine `devenum` device path is `dummy_videoN`; plugin needs `vid_bcdc&pid_000a/b` | `shims/qtmm` |
| Process gives a near-empty mesh (e.g. 4.8 M fused points → 4k Poisson vertices → 274 after trim) | Wine's builtin `vcomp140` (OpenMP) breaks `meshrecon.dll`'s parallel Poisson iso-surface extraction; Wine prefers it over the app-local Microsoft copy | `WINEDLLOVERRIDES=vcomp140=n,b` in `run.sh` (→ 409k vertices on the same data) |
| Brightness slider has no effect | Wine `qcap` has no `IAMCameraControl`; exposure/gain never reach V4L2 | `shims/cvcap` + `tools/lizard_ctrl_bridge.py` |

Wine enumerates DirectShow video devices as the V4L2 capture nodes in numeric order
(metadata nodes skipped), so DirectShow index N = N-th capture node.
