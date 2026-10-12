#!/usr/bin/env python3
"""Apply CRStudio camera settings to V4L2 while it runs under Wine.

Wine's DirectShow capture ignores exposure/gain, so CRStudio's Brightness and
Sensitivity sliders do nothing. The opencv_highgux2413.dll shim forwards every
cv::VideoCapture::set() as a UDP datagram "<dshow-index> <prop> <value*1000>"
to 127.0.0.1:47811; this helper maps it to a V4L2 control.

DirectShow index N is the N-th V4L2 video-capture node in numeric order, the
same order Wine's devenum uses. Nodes are re-enumerated for every setting so a
replugged scanner (new /dev/video numbers) is still addressed correctly.
"""
import fcntl, glob, os, re, socket, struct, subprocess, sys

PORT = 47811
VIDIOC_QUERYCAP = 0x80685600
V4L2_CAP_VIDEO_CAPTURE = 0x1


def capture_nodes():
    nodes = []
    for path in sorted(glob.glob("/dev/video*"), key=lambda p: int(re.sub(r"\D", "", p) or 0)):
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            continue
        try:
            buf = bytearray(104)
            fcntl.ioctl(fd, VIDIOC_QUERYCAP, buf)
            caps, device_caps = struct.unpack_from("<II", buf, 84)
            if (device_caps if caps & 0x80000000 else caps) & V4L2_CAP_VIDEO_CAPTURE:
                nodes.append(path)
        except OSError:
            pass
        finally:
            os.close(fd)
    return nodes


def exposure_100us(log2_seconds):
    # Windows UVC driver: CameraControl_Exposure is log2(seconds);
    # UVC exposure_time_absolute is in 100 µs units.
    return round((2.0 ** log2_seconds) * 10000)


# OpenCV 2.4 CV_CAP_PROP_* -> (v4l2 control, converter)
CONTROLS = {
    10: ("brightness", round),
    11: ("contrast", round),
    12: ("saturation", round),
    13: ("hue", round),
    14: ("gain", round),
    15: ("exposure_time_absolute", exposure_100us),
    20: ("sharpness", round),
    22: ("gamma", round),
    27: ("zoom_absolute", round),
    35: ("gain", round),  # uvcCameraPlugin uses Roll as gain on some models
}


def describe(nodes):
    return ", ".join(f"{i}={n}" for i, n in enumerate(nodes))


def main():
    nodes = capture_nodes()
    print("capture nodes:", describe(nodes), flush=True)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("127.0.0.1", PORT))
    except OSError as e:
        sys.exit(f"port {PORT} busy ({e}); is another bridge running?")
    while True:
        data, _ = s.recvfrom(256)
        try:
            index, prop, milli = (int(x) for x in data.split())
        except ValueError:
            continue
        if prop not in CONTROLS:
            continue
        current = capture_nodes()
        if current != nodes:
            nodes = current
            print("capture nodes changed:", describe(nodes), flush=True)
        if not 0 <= index < len(nodes):
            continue
        name, conv = CONTROLS[prop]
        value = conv(milli / 1000.0)
        dev = nodes[index]
        r = subprocess.run(["v4l2-ctl", "-d", dev, "-c", f"{name}={value}"],
                           capture_output=True, text=True)
        status = "ok" if r.returncode == 0 else r.stderr.strip()
        print(f"{dev} prop {prop} = {milli / 1000.0:g} -> {name}={value} [{status}]", flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
