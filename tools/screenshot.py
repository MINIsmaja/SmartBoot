#!/usr/bin/env python3
"""Save the Cardputer's current SmartBoot screen as a PNG, optionally pressing keys first.

    python3 tools/screenshot.py shot.png
    python3 tools/screenshot.py store.png --keys ok,down     # press keys, then capture
"""
import argparse
import time

import serial
from PIL import Image

KEY_SETTLE_S = 0.35  # time for animations to finish after each key


def open_port(port):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 5
    s.dtr = s.rts = True
    s.open()
    # Opening the USB-CDC port can reboot the S3; wait for it to come back and
    # drain the boot log before sending commands.
    time.sleep(1.8)
    s.reset_input_buffer()
    return s


def wait_for(s, prefix):
    while True:
        line = s.readline().decode(errors="replace").strip()
        if not line:
            raise TimeoutError(f"no '{prefix}' reply from the device")
        if line.startswith(prefix):
            return line


def press(s, key):
    s.write(f"key {key}\n".encode())
    wait_for(s, "OK")
    time.sleep(KEY_SETTLE_S)


def capture(s):
    s.reset_input_buffer()
    s.write(b"shot\n")
    _, w, h = wait_for(s, "SHOT").split()
    w, h = int(w), int(h)
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        row = bytes.fromhex(s.readline().decode().strip())
        for x in range(w):
            v = (row[x * 2] << 8) | row[x * 2 + 1]  # big-endian RGB565
            px[x, y] = ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
    wait_for(s, "END")
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--keys", default="", help="comma-separated: up,down,left,right,ok,back,tab or a character")
    ap.add_argument("--scale", type=int, default=3)
    args = ap.parse_args()

    with open_port(args.port) as s:
        for key in filter(None, args.keys.split(",")):
            press(s, key)
        img = capture(s)
    img.resize((img.width * args.scale, img.height * args.scale), Image.NEAREST).save(args.out)
    print(f"saved {args.out}")


if __name__ == "__main__":
    main()
