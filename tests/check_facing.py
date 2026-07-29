#!/usr/bin/env python3
"""
Regression test: ship sprite facing direction.

Drives the ROM (via tests/runner) to accelerate in each of the four
cardinal directions, screenshots each state, then template-matches the
on-screen ship against the 24 original ship frames from xquest.gfx.

Expected mapping (as in the DOS original, theta = arctan(delx/dely)):
  frame 0 = facing DOWN, 6 = RIGHT, 12 = UP, 18 = LEFT.
"""
import re
import struct
import subprocess
import sys
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CORE = os.environ.get("CORE", "/home/user/tools/picodrive/picodrive_libretro.so")
ROM = os.environ.get("ROM", os.path.join(ROOT, "build", "xquest32x.32x"))
ART = os.path.join(HERE, "artifacts")

SCRIPT = """\
run 420
press START 5
run 30
press B 5
run 5
press DOWN 12
shot {art}/face_down.ppm
press A 30
press UP 18
shot {art}/face_up.ppm
press A 30
press RIGHT 15
shot {art}/face_right.ppm
press A 30
press LEFT 15
shot {art}/face_left.ppm
"""

EXPECTED = {"face_down": 0, "face_up": 12, "face_right": 6, "face_left": 18}


def load_frames():
    d = open(os.path.join(ROOT, "assets", "xquest.gfx"), "rb").read()
    pos = 0
    frames = []
    while len(frames) < 24:
        w, h = struct.unpack_from("<hh", d, pos)
        bw = ((w - 1) // 4 + 1) * 4
        px = np.frombuffer(d[pos + 4:pos + 4 + bw * h],
                           dtype=np.uint8).reshape(h, bw)
        frames.append(px)
        pos += 4 + bw * h
    return frames


def load_palette():
    pt = re.sub(r"\{[^}]*\}", "",
                open(os.path.join(ROOT, "assets", "palette.inc")).read())
    nums = [int(x) for x in re.findall(r"-?\d+", pt)][4:4 + 768]

    def to_rgb(ci):
        ci = int(ci)
        r, g, b = nums[ci * 3:ci * 3 + 3]
        # our CRAM conversion + libretro 555 -> 888 expansion
        return tuple((((v * 31 + 31) // 63) << 3) for v in (r, g, b))

    return to_rgb


def best_frame(ppm, frames, to_rgb):
    a = np.array(Image.open(ppm)).astype(np.int32)
    results = []
    for fi, fr in enumerate(frames):
        h, bw = fr.shape
        target = np.zeros((h, bw, 3), np.int32)
        mask = fr != 0
        for y in range(h):
            for x in range(bw):
                if fr[y, x]:
                    target[y, x] = to_rgb(fr[y, x])
        best = None
        for yy in range(25, a.shape[0] - h):
            for xx in range(0, a.shape[1] - bw):
                win = a[yy:yy + h, xx:xx + bw, :3]
                diff = np.abs(win - target)[mask].mean()
                if best is None or diff < best:
                    best = diff
        results.append((best, fi))
    results.sort()
    return results[0]


def main():
    os.makedirs(ART, exist_ok=True)
    script_path = os.path.join(ART, "facing.script")
    with open(script_path, "w") as f:
        f.write(SCRIPT.format(art=ART))

    r = subprocess.run([os.path.join(HERE, "runner"), CORE, ROM, script_path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout)
        print("FAIL: runner exited nonzero")
        return 1

    frames = load_frames()
    to_rgb = load_palette()
    fail = 0
    for name, exp in EXPECTED.items():
        diff, got = best_frame(os.path.join(ART, name + ".ppm"),
                               frames, to_rgb)
        ok = (got == exp and diff < 8.0)
        print(f"{'PASS' if ok else 'FAIL'}: {name}: frame {got} "
              f"(expected {exp}, match diff {diff:.1f})")
        if not ok:
            fail = 1
    return fail


if __name__ == "__main__":
    sys.exit(main())
