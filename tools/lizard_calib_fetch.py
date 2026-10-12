#!/usr/bin/env python3
"""Download the factory calibration for a CR-Scan Lizard by serial number.

Re-implementation of cloudServicePlugin.dll (CRStudio 2.5.4):
  0. SN trimmed per model (Lizard: last 7 chars, CRSLxxxxxxx -> xxxxxxx)
  1. POST https://sw.3dyunzhan.com/v2/swift/calibration/find
       headers: sn: <SN>, sign: md5hex(md5hex(<SN> + KEY)), Content-Type: application/json
       (KEY is read from the user's CRStudio: plugins/cloudServicePlugin.dll)
       body:    {"sn": "<SN>"}
     response: {"code": "200", ..., "data": {"uploadUrl": ..., "createTime": ..., ...}}
  2. GET <uploadUrl>  (headers sn: "1", sign derived from "1")  -> calib.txt
  3. For the Lizard (ScannerType 2) CRStudio splits calib.txt into calibnear.txt / calibfar.txt
     (CalibFileParser::SplitCabFile).

The serial number is read from the scanner with:  lizard_xu.py info
"""
import argparse, hashlib, json, os, re, sys, urllib.request

BASE = "https://sw.3dyunzhan.com/"
FIND = "v2/swift/calibration/find"
KEY_PATTERN = re.compile(rb"(?<![A-Za-z0-9+/=])[A-Za-z0-9+/]{43}=(?![A-Za-z0-9+/=])")


def load_key(crstudio_dir):
    """Read the request-signing key from the user's own CRStudio install.

    It is the only 44-character base64 string in plugins/cloudServicePlugin.dll
    (stored right after the API base URL).
    """
    dll = os.path.join(crstudio_dir, "plugins", "cloudServicePlugin.dll")
    keys = set(KEY_PATTERN.findall(open(dll, "rb").read()))
    if len(keys) != 1:
        sys.exit(f"could not find a unique signing key in {dll} ({len(keys)} candidates)")
    return keys.pop().decode()


def sign(value, key):
    inner = hashlib.md5((value + key).encode()).hexdigest()
    return hashlib.md5(inner.encode()).hexdigest()


def trim_sn(sn):
    # core.dll CommonTools::TrimDeviceSN: CR-Scan Lizard (CRSL + 11 chars) -> last 7 chars
    if sn.startswith("CRSL") and len(sn) == 11:
        return sn[-7:]
    return sn


def request(url, sn_header, key, body=None, content_type="application/json"):
    req = urllib.request.Request(url, data=body, method="POST" if body is not None else "GET")
    req.add_header("sn", sn_header)
    req.add_header("sign", sign(sn_header, key))
    req.add_header("Content-Type", content_type)
    with urllib.request.urlopen(req, timeout=15) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sn", help="scanner serial, e.g. CRSLxxxxxxx")
    ap.add_argument("-o", "--out", default="calib.txt")
    ap.add_argument("--crstudio", required=True, help="CRStudio install folder (signing key is read from it)")
    a = ap.parse_args()
    key = load_key(a.crstudio)

    sn = trim_sn(a.sn)
    body = json.dumps({"sn": sn}).encode()
    resp = json.loads(request(BASE + FIND, sn, key, body))
    print(json.dumps(resp, indent=2))
    if str(resp.get("code")) != "200":
        sys.exit("calibration lookup failed")
    url = resp["data"]["uploadUrl"]
    data = request(url, "1", key, content_type="application/octet-stream")
    open(a.out, "wb").write(data)
    print(f"saved {len(data)} bytes -> {a.out}")


if __name__ == "__main__":
    main()
