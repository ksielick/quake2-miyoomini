"""Compare two sets of timedemo screenshots (miyoo/tools/bench.sh SHOTS=N) pixel for pixel.
From tyrquake-miyoo (miyoo/tools/shots_compare.py); added: Quake II's 32-bit .argb shots.

    py -3 miyoo/tools/shots_compare.py <reference dir> <new dir> [--out DIR] [--size 640x480]

Both dirs hold <demo>/NNNNN.argb (the frame as the renderer made it: 32-bit 0xAARRGGBB,
little-endian; miyoo/platform/miyoo_sdl.c SaveShot) or <demo>/NNNNN.rgb565 (TyrQuake's). A
timedemo replays the same frames, so a correct build gives identical shots. For each shot that differs:
the number of pixels, their bounding box and the largest channel difference, and
<out>/<demo>_NNNNN.png = reference | new | changed pixels (magenta on a dimmed reference).
Also <out>/contact_<demo>.png: every shot of the new set. Exit code 1 if anything differs or
is missing.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


SHOT_EXT = (".argb", ".rgb565")


def load(path, w, h):
    if path.endswith(".argb"):
        raw = np.fromfile(path, dtype=np.uint8)
        if raw.size != w * h * 4:
            raise ValueError(f"{path}: {raw.size // 4} pixels, not {w}x{h}")
        bgra = raw.reshape(h, w, 4)
        return np.dstack([bgra[:, :, 2], bgra[:, :, 1], bgra[:, :, 0]])
    raw = np.fromfile(path, dtype="<u2")
    if raw.size != w * h:
        raise ValueError(f"{path}: {raw.size} pixels, not {w}x{h}")
    raw = raw.reshape(h, w).astype(np.uint32)
    r = ((raw >> 11) & 31) * 255 // 31
    g = ((raw >> 5) & 63) * 255 // 63
    b = (raw & 31) * 255 // 31
    return np.dstack([r, g, b]).astype(np.uint8)


def contact(paths, w, h, out, cols=6):
    if not paths:
        return
    tw, th = w // 4, h // 4
    rows = (len(paths) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (tw + 4), rows * (th + 4)), "white")
    for i, p in enumerate(paths):
        im = Image.fromarray(load(p, w, h)).resize((tw, th))
        sheet.paste(im, ((i % cols) * (tw + 4), (i // cols) * (th + 4)))
    sheet.save(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("new")
    ap.add_argument("--out", help="where the diff pictures go (default: <new>/diff)")
    ap.add_argument("--size", default="640x480")
    a = ap.parse_args()
    w, h = (int(v) for v in a.size.split("x"))
    out = a.out or os.path.join(a.new, "diff")
    os.makedirs(out, exist_ok=True)

    bad = 0
    demos = sorted(d for d in os.listdir(a.ref) if os.path.isdir(os.path.join(a.ref, d)))
    for demo in demos:
        rdir, ndir = os.path.join(a.ref, demo), os.path.join(a.new, demo)
        rshots = sorted(f for f in os.listdir(rdir) if f.endswith(SHOT_EXT))
        nshots = sorted(f for f in os.listdir(ndir) if f.endswith(SHOT_EXT)) if os.path.isdir(ndir) else []
        same = 0
        for f in rshots:
            if f not in nshots:
                print(f"{demo} {f}: missing in the new set")
                bad += 1
                continue
            rp, np_ = os.path.join(rdir, f), os.path.join(ndir, f)
            with open(rp, "rb") as x, open(np_, "rb") as y:
                if x.read() == y.read():
                    same += 1
                    continue
            ri, ni = load(rp, w, h), load(np_, w, h)
            delta = np.abs(ri.astype(np.int16) - ni.astype(np.int16)).max(axis=2)
            ys, xs = np.nonzero(delta)
            print(f"{demo} {f}: {len(xs)} pixels differ ({100.0 * len(xs) / (w * h):.2f} %), "
                  f"box x {xs.min()}-{xs.max()} y {ys.min()}-{ys.max()}, max channel diff {delta.max()}")
            mark = (ri // 3).copy()
            mark[delta > 0] = (255, 0, 255)
            Image.fromarray(np.hstack([ri, ni, mark])).save(
                os.path.join(out, f"{demo}_{os.path.splitext(f)[0]}.png"))
            bad += 1
        extra = [f for f in nshots if f not in rshots]
        print(f"{demo}: {same}/{len(rshots)} identical" + (f", {len(extra)} only in the new set" if extra else ""))
        contact([os.path.join(ndir, f) for f in nshots], w, h, os.path.join(out, f"contact_{demo}.png"))
    print("OK: all identical" if not bad else f"DIFFERENT: {bad} shot(s), pictures in {out}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
