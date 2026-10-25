"""Pixel diff of two folders of stills with the same file names (A/B renders, e.g. the character snow layer at 0:
Saved/Screenshots/Environment/charsnow/ab_before vs ab_after; docs/systems/environment-weather.md section 15).

Usage: python Scripts/Tools/image_diff.py <dirA> <dirB> [--pattern *_zero.png] [--heatmap <outdir>]
Per file: mean absolute difference (0..255 per channel), the 99.9th percentile, the max, and the share of pixels differing by more
than 2 / 8 levels. Optional heat maps (x8 amplified) for a look.
"""
import argparse
import fnmatch
import os
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.int16)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--pattern", default="*.png")
    parser.add_argument("--heatmap", default="")
    args = parser.parse_args()
    names = sorted(n for n in os.listdir(args.a) if fnmatch.fnmatch(n, args.pattern) and os.path.exists(os.path.join(args.b, n)))
    if not names:
        print("no common files")
        return 1
    print("%-48s %8s %8s %6s %9s %9s" % ("file", "mean", "p99.9", "max", ">2 lvl %", ">8 lvl %"))
    for name in names:
        a = load(os.path.join(args.a, name))
        b = load(os.path.join(args.b, name))
        if a.shape != b.shape:
            print("%-48s size mismatch %s vs %s" % (name, a.shape, b.shape))
            continue
        diff = np.abs(a - b).max(axis=2)
        print("%-48s %8.4f %8.2f %6d %9.4f %9.4f" % (name, np.abs(a - b).mean(), np.percentile(diff, 99.9), diff.max(),
                                                 (diff > 2).mean() * 100.0, (diff > 8).mean() * 100.0))
        if args.heatmap:
            os.makedirs(args.heatmap, exist_ok=True)
            Image.fromarray(np.clip(diff * 8, 0, 255).astype(np.uint8)).save(os.path.join(args.heatmap, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
