#!/usr/bin/env python3
"""CR-Scan Lizard UVC extension-unit access (Linux port of HCamera_x64.dll hRcl* API).

Reverse engineered from HCamera_x64.dll:
  XU GUID {1229a78c-47b4-4094-b0ce-db07386fb938}, unit id 4
  selector 10: 8-byte command (SET_CUR)
  selector 11: 8-byte data chunk (GET_CUR for reads, SET_CUR for writes)
"""
import argparse, ctypes, fcntl, os, struct, sys

UNIT = 4
SEL_CMD, SEL_DATA = 10, 11
UVC_SET_CUR, UVC_GET_CUR, UVC_GET_LEN = 0x01, 0x81, 0x85


class uvc_xu_control_query(ctypes.Structure):
    _fields_ = [("unit", ctypes.c_uint8), ("selector", ctypes.c_uint8),
                ("query", ctypes.c_uint8), ("size", ctypes.c_uint16),
                ("data", ctypes.c_void_p)]


# _IOWR('u', 0x21, struct uvc_xu_control_query)
UVCIOC_CTRL_QUERY = (3 << 30) | (ctypes.sizeof(uvc_xu_control_query) << 16) | (ord('u') << 8) | 0x21


class LizardXU:
    def __init__(self, dev):
        self.fd = os.open(dev, os.O_RDWR)

    def close(self):
        os.close(self.fd)

    def _query(self, sel, query, buf):
        q = uvc_xu_control_query(UNIT, sel, query, len(buf),
                                 ctypes.cast(buf, ctypes.c_void_p))
        fcntl.ioctl(self.fd, UVCIOC_CTRL_QUERY, q)

    def get_len(self, sel):
        b = (ctypes.c_uint8 * 2)()
        self._query(sel, UVC_GET_LEN, b)
        return b[0] | (b[1] << 8)

    def _cmd(self, cmd):
        b = (ctypes.c_uint8 * 8)(*cmd)
        self._query(SEL_CMD, UVC_SET_CUR, b)

    def _read(self, n):
        out = bytearray()
        while len(out) < n:
            b = (ctypes.c_uint8 * 8)()
            self._query(SEL_DATA, UVC_GET_CUR, b)
            out += bytes(b)
        return bytes(out[:n])

    def _write(self, data):
        for i in range(0, len(data), 8):
            chunk = data[i:i + 8].ljust(8, b"\0")
            b = (ctypes.c_uint8 * 8)(*chunk)
            self._query(SEL_DATA, UVC_SET_CUR, b)

    # --- hRcl* equivalents -------------------------------------------------
    def read_flash(self, addr, n):
        self._cmd([0x02, 0xC2, addr & 0xFF, (addr >> 8) & 0xFF,
                   n & 0xFF, (n >> 8) & 0xFF, (addr >> 16) & 0xFF, 0])
        return self._read(n)

    def read_reg16(self, reg):
        self._cmd([0x00, 0xCA, reg & 0xFF, reg >> 8, 2, 0, 0, 0])
        d = self._read(2)
        return (d[0] << 8) | d[1]

    def write_reg16(self, reg, val):
        self._cmd([0x00, 0x8A, reg & 0xFF, reg >> 8, 2, 0, 0, 0])
        self._write(bytes([val >> 8, val & 0xFF]))

    def read_mem(self, addr, n=1):
        assert n <= 8
        self._cmd([0x00, 0xC2, addr & 0xFF, addr >> 8, n, 0, 0, 0])
        return self._read(n)

    def write_mem(self, addr, data):
        assert len(data) <= 8
        self._cmd([0x00, 0x82, addr & 0xFF, addr >> 8, len(data), 0, 0, 0])
        self._write(data)

    def serial_number(self):
        # scanCore DeviceHelper::GetUvcDeviceSN: 20 bytes @0xfe00, SN = bytes 4..18
        raw = self.read_flash(0xFE00, 0x14)
        return raw, raw[4:19].split(b"\0")[0].decode("latin1")


def hexdump(data, base=0):
    for i in range(0, len(data), 16):
        c = data[i:i + 16]
        print(f"{base + i:08x}  {c.hex(' '):<47}  {''.join(chr(x) if 32 <= x < 127 else '.' for x in c)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--device", default="/dev/video2")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("info")
    p = sub.add_parser("flash"); p.add_argument("addr", type=lambda s: int(s, 0)); p.add_argument("length", type=lambda s: int(s, 0))
    p.add_argument("-o", "--out")
    p = sub.add_parser("reg"); p.add_argument("addr", type=lambda s: int(s, 0)); p.add_argument("value", nargs="?", type=lambda s: int(s, 0))
    p = sub.add_parser("mem"); p.add_argument("addr", type=lambda s: int(s, 0)); p.add_argument("length", nargs="?", type=lambda s: int(s, 0), default=1)
    a = ap.parse_args()

    xu = LizardXU(a.device)
    try:
        if a.cmd == "info":
            print("XU len sel10:", xu.get_len(SEL_CMD), "sel11:", xu.get_len(SEL_DATA))
            raw, sn = xu.serial_number()
            hexdump(raw, 0xFE00)
            print("SN:", sn)
        elif a.cmd == "flash":
            data = bytearray()
            for off in range(0, a.length, 0x400):
                data += xu.read_flash(a.addr + off, min(0x400, a.length - off))
            if a.out:
                open(a.out, "wb").write(data)
                print(f"wrote {len(data)} bytes to {a.out}")
            else:
                hexdump(data, a.addr)
        elif a.cmd == "reg":
            if a.value is None:
                print(f"reg 0x{a.addr:04x} = 0x{xu.read_reg16(a.addr):04x}")
            else:
                xu.write_reg16(a.addr, a.value)
        elif a.cmd == "mem":
            hexdump(xu.read_mem(a.addr, a.length), a.addr)
    finally:
        xu.close()


if __name__ == "__main__":
    main()
