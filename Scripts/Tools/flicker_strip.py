"""Start-flicker luminance strips (docs/systems/environment-weather.md section 19).

Reads the per-frame CSVs written by CodexTactics.StartFlickerShots (Saved/Screenshots/Environment/flicker/<tag>/<map>.csv) and, when
present, the run's log next to it (<map>.log: "[EnvTrace] f<engine frame> t<time> <event>" lines), and draws per tag:
  strip_<tag>.png  - a row per map: the mean luma per frame (bold) + the 4 bands (thin) + the frame-to-frame cell diff (bars), shaded
                     camera-fade / exposure-snap spans, vertical ticks at traced events; under it thumbnails of the frames around the
                     largest jumps.
and with two tags (before / after) compare_<before>_<after>.png - both means per map on one axis.
Prints a table: per map the largest |mean step| outside camera fades and the largest cell diff, with the events within +-1 frame.

usage: python Scripts/Tools/flicker_strip.py <flicker dir> <tag> [<tag2>]
"""

import csv
import os
import re
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from PIL import Image  # noqa: E402

TRACE = re.compile(r"\[EnvTrace\] f(\d+) t(-?[\d.]+) (.*)$")


def load_csv(path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k, v in r.items():
            try:
                r[k] = float(v)
            except ValueError:
                pass
    return rows


def load_events(path):
    events = []
    if not os.path.exists(path):
        return events
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = TRACE.search(line)
            if m:
                events.append((int(m.group(1)), float(m.group(2)), m.group(3).strip()))
    return events


def steps(rows):
    """(frame index, |mean step|) outside camera fades."""
    out = []
    for i in range(1, len(rows)):
        a, b = rows[i - 1], rows[i]
        if a["fade"] > 0.001 or b["fade"] > 0.001:
            continue
        out.append((i, abs(b["mean"] - a["mean"])))
    return out


def frame_of_engine(rows, engine_frame):
    best = None
    for i, r in enumerate(rows):
        if r["engine_frame"] <= engine_frame:
            best = i
    return best


def analyse(flicker_dir, tag):
    tag_dir = os.path.join(flicker_dir, tag)
    maps = sorted(f[:-4] for f in os.listdir(tag_dir) if f.endswith(".csv"))
    result = {}
    for m in maps:
        rows = load_csv(os.path.join(tag_dir, m + ".csv"))
        events = load_events(os.path.join(tag_dir, m + ".log"))
        st = steps(rows)
        top = sorted(st, key=lambda s: -s[1])[:3]
        result[m] = (rows, events, st, top)
    return result


def draw_strip(flicker_dir, tag, data):
    maps = list(data.keys())
    n = len(maps)
    fig = plt.figure(figsize=(16, 4.6 * n))
    gs = fig.add_gridspec(n * 2, 1, height_ratios=[3, 1.4] * n, hspace=0.45)
    for k, m in enumerate(maps):
        rows, events, st, top = data[m]
        ax = fig.add_subplot(gs[k * 2])
        x = [int(r["frame"]) for r in rows]
        ax.plot(x, [r["mean"] for r in rows], color="black", lw=2, label="mean")
        for band, col in (("top", "#4a7bd0"), ("middle", "#3aa36b"), ("bottom", "#c9822b"), ("hud", "#a24bb5")):
            ax.plot(x, [r[band] for r in rows], color=col, lw=0.8, alpha=0.8, label=band)
        ax2 = ax.twinx()
        ax2.bar(x, [r["diff"] for r in rows], color="#d04a4a", alpha=0.35, width=1.0, label="cell diff")
        ax2.set_ylabel("cell diff", color="#d04a4a")
        for i, r in enumerate(rows):
            if r["fade"] > 0.001:
                ax.axvspan(r["frame"] - 0.5, r["frame"] + 0.5, color="#888888", alpha=0.25, lw=0)
            elif r["snap"] > 0.5:
                ax.axvspan(r["frame"] - 0.5, r["frame"] + 0.5, color="#f2d36b", alpha=0.18, lw=0)
            if r.get("flash", 0) > 0.001:
                ax.axvspan(r["frame"] - 0.5, r["frame"] + 0.5, color="#5fb0ff", alpha=0.5, lw=0)
        for ef, _t, text in events:
            fi = frame_of_engine(rows, ef)
            if fi is None:
                continue
            ax.axvline(rows[fi]["frame"], color="#555555", lw=0.5, ls=":")
        for fi, s in top:
            ax.annotate(f"{s:+.1f}" if s else "0", (rows[fi]["frame"], rows[fi]["mean"]), xytext=(0, 12), textcoords="offset points",
                        ha="center", fontsize=8, color="#b00000")
        mx = max((s for _, s in st), default=0.0)
        ax.set_title(f"{m} [{tag}] - max |mean step| outside fades {mx:.2f} (grey = camera fade, yellow = exposure snap, blue = lightning)")
        ax.set_xlim(0.5, len(rows) + 0.5)
        ax.set_ylim(0, 255)
        ax.set_ylabel("luma 0..255")
        ax.set_xlabel("frame (60 fps fixed step)")
        ax.legend(loc="upper right", fontsize=7, ncol=5)
        # Thumbnails around the largest jumps.
        axt = fig.add_subplot(gs[k * 2 + 1])
        axt.axis("off")
        thumbs = []
        for fi, s in top[:2]:
            for d in (-1, 0, 1):
                j = fi + d
                if 0 <= j < len(rows):
                    thumbs.append((int(rows[j]["frame"]), s if d == 0 else None))
        if thumbs:
            tdir = os.path.join(flicker_dir, tag, m)
            imgs = []
            for f, _s in thumbs:
                p = os.path.join(tdir, f"{m}_f{f:03d}.png")
                imgs.append(Image.open(p).convert("RGB") if os.path.exists(p) else Image.new("RGB", (320, 180), (255, 0, 255)))
            w, h = imgs[0].size
            sheet = Image.new("RGB", (w * len(imgs) + 4 * (len(imgs) - 1), h), (255, 255, 255))
            for i, im in enumerate(imgs):
                sheet.paste(im.resize((w, h)), (i * (w + 4), 0))
            axt.imshow(sheet)
            axt.set_title("frames " + ", ".join(f"f{f}" + (f" (step {s:.1f})" if s is not None else "") for f, s in thumbs), fontsize=8)
    out = os.path.join(flicker_dir, f"strip_{tag}.png")
    fig.savefig(out, dpi=80, bbox_inches="tight")
    plt.close(fig)
    return out


def draw_compare(flicker_dir, tag_a, data_a, tag_b, data_b):
    maps = [m for m in data_a if m in data_b]
    fig, axes = plt.subplots(len(maps), 1, figsize=(16, 3.4 * len(maps)), squeeze=False)
    for k, m in enumerate(maps):
        ax = axes[k][0]
        for tag, data, col in ((tag_a, data_a, "#d04a4a"), (tag_b, data_b, "#2a6fd0")):
            rows = data[m][0]
            mx = max((s for _, s in data[m][2]), default=0.0)
            ax.plot([r["frame"] for r in rows], [r["mean"] for r in rows], color=col, lw=1.6, label=f"{tag} (max step {mx:.2f})")
        ax.set_title(m)
        ax.set_ylim(0, 255)
        ax.set_ylabel("mean luma")
        ax.legend(loc="upper right", fontsize=8)
    axes[-1][0].set_xlabel("frame (60 fps fixed step)")
    out = os.path.join(flicker_dir, f"compare_{tag_a}_{tag_b}.png")
    fig.savefig(out, dpi=80, bbox_inches="tight")
    plt.close(fig)
    return out


def report(tag, data):
    print(f"== {tag}")
    for m, (rows, events, st, top) in data.items():
        mx = max(st, key=lambda s: s[1]) if st else (0, 0.0)
        md = max(((i, r["diff"]) for i, r in enumerate(rows) if r["fade"] <= 0.001), key=lambda s: s[1], default=(0, 0.0))
        print(f"  {m}: {len(rows)} frames; max |mean step| {mx[1]:.2f} at frame {int(rows[mx[0]]['frame']) if rows else 0}; "
              f"max cell diff {md[1]:.2f} at frame {int(rows[md[0]]['frame']) if rows else 0}")
        for fi, s in top:
            r = rows[fi]
            near = [e for e in events if abs(e[0] - r["engine_frame"]) <= 2]
            print(f"    step {s:.2f} at frame {int(r['frame'])} (engine {int(r['engine_frame'])}, t {r['time']:.3f}, exposure {r['exposure']:.4f}, "
                  f"snap {int(r['snap'])}, fade {r['fade']:.2f}, flash {r['flash']:.2f})")
            for e in near:
                print(f"      event f{e[0]} t{e[1]:.3f}: {e[2]}")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    flicker_dir = sys.argv[1]
    tags = sys.argv[2:]
    datas = []
    for tag in tags:
        data = analyse(flicker_dir, tag)
        report(tag, data)
        print("  strip:", draw_strip(flicker_dir, tag, data))
        datas.append(data)
    if len(tags) == 2:
        print("compare:", draw_compare(flicker_dir, tags[0], datas[0], tags[1], datas[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
