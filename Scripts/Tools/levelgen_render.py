"""Level generator renders (docs/design/levelgen/LEVEL_GENERATOR.md): top-down PNGs of
  (a) a reference PROFILE (Content/Data/LevelGen/refs/*.json, schema codex.levelgen_ref/1) - optionally over the un-skewed
      reference image - the schematic the game designer checks the reading against;
  (b) a GENERATED layout (schema codex.levelgen_layout/1, the Training Ground generator's output) - the candidate render the
      department page shows next to the reference and the Claude critic sees.
Both share one drawing vocabulary (zones tinted by kind, buildings by height class, the cut, ladders, routes, start / end,
spawns / ambush points), so the designer compares like with like. Pure PIL; no Unreal.

Usage:
  python Scripts/Tools/levelgen_render.py profile <ref.json> <out.png> [--background <unskewed map png> --ppm 10]
  python Scripts/Tools/levelgen_render.py layout  <layout.json> <out.png> [--ppm 8] [--floor 0|1|2]
  python Scripts/Tools/levelgen_render.py unskew  <clean.webp> <annotated.jpg> <out_dir>   (the reference's top-down images)
"""
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ZONE_TINT = {
    "street": (120, 120, 130), "facade": (90, 70, 60), "compound": (150, 110, 60), "t_junction": (140, 140, 90), "industrial_hall": (110, 80, 50),
    "block": (110, 90, 70), "warehouse": (120, 100, 80), "junkyard": (170, 90, 40), "cut": (40, 60, 110), "tank_farm": (130, 90, 70),
    "open": (60, 130, 70), "ruin_two_level": (150, 60, 50), "yard": (120, 110, 80), "pipe_yard": (130, 100, 60),
    # generated layouts (codex.levelgen_layout/1) use the lab's zone kinds too
    "corridor": (160, 140, 60), "wall_run": (100, 100, 170), "gap_run": (170, 100, 140), "corner": (60, 160, 130), "wall_end": (180, 120, 60),
    "alley": (150, 150, 100), "square": (60, 130, 70), "dead_end": (170, 80, 80),
}
HEIGHT_FILL = {"tall": (150, 120, 100), "low": (170, 150, 120), "platform": (120, 140, 160), "fence": None, "tower": (100, 100, 110)}
FONT = ImageFont.load_default()


def poly_px(poly, ppm, oy=0):
    return [(p[0] * ppm, p[1] * ppm + oy) for p in poly]


def draw_profile(ref, ppm=10, background=None):
    w, h = ref["bounds"]["max"]
    W, H = int(w * ppm), int(h * ppm)
    img = Image.new("RGB", (W, H + 90), (25, 28, 34))
    if background:
        bg = Image.open(background).convert("RGB").resize((W, H), Image.LANCZOS)
        img.paste(bg, (0, 0))
    over = Image.new("RGBA", img.size, (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    for z in ref["zones"]:
        c = ZONE_TINT.get(z["kind"], (150, 150, 150))
        od.polygon(poly_px(z["poly"], ppm), fill=c + (70,), outline=c + (230,))
    img.paste(Image.alpha_composite(img.convert("RGBA"), over).convert("RGB"))
    d = ImageDraw.Draw(img)
    # the cut
    cut = ref.get("cut")
    if cut:
        (ax, ay), (bx, by) = cut["axis"]
        half = (cut["trench_width_m"] / 2 + cut["wall_thickness_m"]) * ppm
        d.line([(ax * ppm, ay * ppm), (bx * ppm, by * ppm)], fill=(70, 90, 160), width=int(half * 2))
        d.line([(ax * ppm, ay * ppm), (bx * ppm, by * ppm)], fill=(30, 40, 80), width=int(cut["trench_width_m"] * ppm))
        for x in cut.get("crossings", []):
            px, py = x["pos"][0] * ppm, x["pos"][1] * ppm
            d.rectangle([px - x["width_m"] * ppm / 2, py - half, px + x["width_m"] * ppm / 2, py + half], fill=(200, 200, 190), outline=(255, 255, 255))
            d.text((px + 4, py - half - 12), x["id"], fill=(255, 255, 255), font=FONT)
    for b in ref["buildings"]:
        (x0, y0), (x1, y1) = b["footprint"]
        fill = HEIGHT_FILL.get(b["height_class"], (160, 160, 160))
        box = [x0 * ppm, y0 * ppm, x1 * ppm, y1 * ppm]
        if fill is None:
            d.rectangle(box, outline=(230, 230, 120), width=2)
        else:
            d.rectangle(box, fill=fill, outline=(255, 255, 255), width=2 if b["height_class"] == "tall" else 1)
        if b.get("roof_access"):
            d.rectangle([box[0] + 3, box[1] + 3, box[2] - 3, box[3] - 3], outline=(80, 220, 120), width=2)
        for door in b.get("doors", []):
            d.ellipse([door[0] * ppm - 4, door[1] * ppm - 4, door[0] * ppm + 4, door[1] * ppm + 4], fill=(255, 255, 255))
        if b.get("external_stair"):
            sx, sy = b["external_stair"]
            d.rectangle([sx * ppm - 6, sy * ppm - 6, sx * ppm + 6, sy * ppm + 6], fill=(80, 220, 120))
        d.text((box[0] + 4, box[1] + 4), b["id"], fill=(255, 255, 255), font=FONT)
    for l in ref.get("ladders", []):
        x, y = l["pos"][0] * ppm, l["pos"][1] * ppm
        d.rectangle([x - 6, y - 10, x + 6, y + 10], fill=(255, 230, 0), outline=(0, 0, 0))
        for k in range(-8, 9, 4):
            d.line([(x - 5, y + k), (x + 5, y + k)], fill=(0, 0, 0))
        d.text((x + 9, y - 6), l["id"], fill=(255, 230, 0), font=FONT)
    route = ref["route"]
    for v in route.get("variants", []):
        d.line(poly_px(v["points"], ppm), fill=(90, 200, 255), width=3)
        p = v["points"][len(v["points"]) // 2]
        d.text((p[0] * ppm + 4, p[1] * ppm + 4), v["id"], fill=(90, 200, 255), font=FONT)
    d.line(poly_px(route["main"]["points"], ppm), fill=(0, 140, 255), width=6)
    for i, p in enumerate(route["main"]["points"]):
        d.ellipse([p[0] * ppm - 5, p[1] * ppm - 5, p[0] * ppm + 5, p[1] * ppm + 5], fill=(0, 140, 255), outline=(255, 255, 255))
        d.text((p[0] * ppm + 7, p[1] * ppm - 12), str(i), fill=(200, 230, 255), font=FONT)
    for lane in ref.get("sightlines", {}).get("lanes", []):
        col = (255, 60, 60) if lane["kind"] in ("marksman", "killing_zone") else (255, 150, 60)
        d.line([(lane["a"][0] * ppm, lane["a"][1] * ppm), (lane["b"][0] * ppm, lane["b"][1] * ppm)], fill=col, width=1)
    sx, sy = ref["start"]["pos"]
    d.ellipse([sx * ppm - 14, sy * ppm - 14, sx * ppm + 14, sy * ppm + 14], outline=(0, 230, 90), width=4)
    d.text((sx * ppm + 16, sy * ppm - 6), "START", fill=(0, 230, 90), font=FONT)
    if ref.get("end"):
        ex, ey = ref["end"]["pos"]
        d.rectangle([ex * ppm - 12, ey * ppm - 12, ex * ppm + 12, ey * ppm + 12], outline=(255, 80, 80), width=4)
        d.text((ex * ppm + 16, ey * ppm - 6), "END", fill=(255, 120, 120), font=FONT)
    for z in ref["zones"]:
        cx = sum(p[0] for p in z["poly"]) / len(z["poly"]) * ppm
        cy = sum(p[1] for p in z["poly"]) / len(z["poly"]) * ppm
        d.text((cx - 20, cy - 5), z["id"], fill=(255, 255, 255), font=FONT)
    # metre grid ticks + legend
    for x in range(0, int(w) + 1, 10):
        d.line([(x * ppm, H), (x * ppm, H + 6)], fill=(200, 200, 200))
        d.text((x * ppm + 2, H + 8), str(x), fill=(200, 200, 200), font=FONT)
    legend = ["blue thick = mandatory route (numbered points)", "light blue = route variants V1-V5", "yellow = ladders L1-L4 (rungs)",
              "white box = building (green inner = roof walkable, white dots = doors, green square = external stair)", "yellow outline = fence",
              "red lines = long sightlines (marksman lanes / the cut)", "tints = zones by kind; dark blue band = the cut with its crossings X_*"]
    for i, t in enumerate(legend):
        d.text((8 + (i % 2) * (W // 2), H + 24 + (i // 2) * 12), t, fill=(220, 220, 220), font=FONT)
    d.text((8, H + 72), f"{ref['id']}: {w} x {h} m (scale {ref['scale']['recommended']}), 1 px = {1 / ppm:.2f} m", fill=(255, 255, 0), font=FONT)
    return img


def draw_layout(layout, ppm=8, floor=None):
    """A generated layout (codex.levelgen_layout/1, cm, UE axes X forward = up, Y right = right): walls, buildings per floor, cut,
    ladders, covers, routes, start, spawn / ambush points."""
    (x0, y0), (x1, y1) = layout["bounds"]["min"], layout["bounds"]["max"]
    W, H = int((y1 - y0) / 100 * ppm), int((x1 - x0) / 100 * ppm)
    img = Image.new("RGB", (W, H + 60), (25, 28, 34))
    d = ImageDraw.Draw(img)
    P = lambda p: ((p[1] - y0) / 100 * ppm, (x1 - p[0]) / 100 * ppm)
    for z in layout.get("zones", []):
        c = ZONE_TINT.get(z["kind"], (150, 150, 150))
        d.polygon([P(p) for p in z["poly"]], fill=tuple(int(v * 0.35 + 25 * 0.65) for v in c), outline=c)
    cut = layout.get("cut")
    if cut:
        d.polygon([P(p) for p in cut["poly"]], fill=(30, 40, 80), outline=(70, 90, 160))
        for x in cut.get("crossings", []):
            d.polygon([P(p) for p in x["poly"]], fill=(200, 200, 190))
    for b in layout.get("buildings", []):
        if floor is not None and floor > 0 and b.get("floors", 1) <= floor:
            continue
        fp = [P(p) for p in b["footprint"]]
        fill = (150, 120, 100) if b.get("height", 0) >= 600 else (170, 150, 120)
        d.polygon(fp, fill=fill, outline=(255, 255, 255))
        if b.get("roof_walkable"):
            d.polygon(fp, outline=(80, 220, 120))
        for door in b.get("doors", []):
            q = P(door["pos"] if isinstance(door, dict) else door)
            d.ellipse([q[0] - 3, q[1] - 3, q[0] + 3, q[1] + 3], fill=(255, 255, 255))
        for hole in b.get("roof_holes", []):
            q = P(hole)
            d.rectangle([q[0] - 4, q[1] - 4, q[0] + 4, q[1] + 4], outline=(255, 120, 60), width=2)
        for s in b.get("stairs", []):
            q = P(s["pos"])
            d.rectangle([q[0] - 4, q[1] - 4, q[0] + 4, q[1] + 4], fill=(80, 220, 120))
        d.text((min(p[0] for p in fp) + 2, min(p[1] for p in fp) + 2), b["id"], fill=(255, 255, 255), font=FONT)
    for wall in layout.get("walls", []):
        if floor is not None and wall.get("floor", 0) != floor:
            continue
        a, b = P(wall["a"]), P(wall["b"])
        d.line([a, b], fill=(200, 200, 210) if wall.get("kind", "wall") != "fence" else (230, 230, 120), width=max(1, int(wall["thickness"] / 100 * ppm)))
    for c in layout.get("covers", []):
        q = P(c["pos"])
        r = max(2, c.get("size", [100, 60])[0] / 200 * ppm)
        d.rectangle([q[0] - r, q[1] - r / 2, q[0] + r, q[1] + r / 2], fill=(120, 110, 90), outline=(200, 190, 160))
    for l in layout.get("ladders", []):
        q = P(l["pos"])
        d.rectangle([q[0] - 4, q[1] - 7, q[0] + 4, q[1] + 7], fill=(255, 230, 0), outline=(0, 0, 0))
    for r in layout.get("routes", []):
        main = r.get("mandatory", r["id"] == layout.get("mandatory_route"))
        d.line([P(p) for p in r["points"]], fill=(0, 140, 255) if main else (90, 200, 255), width=5 if main else 2)
    for sp in layout.get("spawn_points", []):
        q = P(sp["pos"])
        d.polygon([(q[0], q[1] - 6), (q[0] + 6, q[1] + 5), (q[0] - 6, q[1] + 5)], fill=(255, 80, 80))
    for ap in layout.get("ambush_points", []):
        q = P(ap["pos"])
        d.ellipse([q[0] - 4, q[1] - 4, q[0] + 4, q[1] + 4], outline=(255, 160, 60), width=2)
    for pr in layout.get("patrol_routes", []):
        d.line([P(p) for p in pr["points"]] + [P(pr["points"][0])], fill=(255, 100, 100), width=1)
    q = P(layout["spawn"]["pos"])
    d.ellipse([q[0] - 10, q[1] - 10, q[0] + 10, q[1] + 10], outline=(0, 230, 90), width=3)
    if layout.get("end"):
        q = P(layout["end"])
        d.rectangle([q[0] - 9, q[1] - 9, q[0] + 9, q[1] + 9], outline=(255, 80, 80), width=3)
    d.text((6, H + 6), f"{layout['name']}  {int((y1 - y0) / 100)} x {int((x1 - x0) / 100)} m  seed {layout.get('seed', '-')}  floor {floor if floor is not None else 'all'}",
           fill=(255, 255, 0), font=FONT)
    d.text((6, H + 22), "blue = mandatory route, light = variants, yellow = ladders, red triangles = spawns, orange rings = ambush points, red loops = patrols",
           fill=(200, 200, 200), font=FONT)
    d.text((6, H + 38), "white = buildings (green = walkable roof, orange squares = roof holes, green squares = stairs), dark blue = the cut", fill=(200, 200, 200), font=FONT)
    return img


def unskew(clean, annotated, out_dir, ppm=10, corners=None, size_m=(177, 124)):
    """The reference's iso rhombus -> a top-down image (least-squares affine over the four corners)."""
    import numpy as np
    corners = corners or {"left": [62, 1141], "top": [2271, 40], "right": [3788, 872], "bottom": [1530, 2103]}
    UV = np.array([[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]], float)
    XY = np.array([corners["left"], corners["top"], corners["right"], corners["bottom"]], float)
    M, *_ = np.linalg.lstsq(UV, XY, rcond=None)
    A, t = M[:2].T, M[2]
    W, H = int(size_m[0] * ppm), int(size_m[1] * ppm)
    data = (A[0, 0] / W, A[0, 1] / H, t[0], A[1, 0] / W, A[1, 1] / H, t[1])
    os.makedirs(out_dir, exist_ok=True)
    for src, name in ((clean, "ref_topdown_clean.png"), (annotated, "ref_topdown_annotated.png")):
        Image.open(src).convert("RGB").transform((W, H), Image.AFFINE, data, resample=Image.BICUBIC).save(os.path.join(out_dir, name))
    return os.path.join(out_dir, "ref_topdown_clean.png")


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    mode = args[0]
    opts = {args[i]: args[i + 1] for i in range(len(args) - 1) if args[i].startswith("--")}
    ppm = float(opts.get("--ppm", 10 if mode == "profile" else 8))
    if mode == "profile":
        ref = json.load(open(args[1], encoding="utf-8"))
        draw_profile(ref, ppm, opts.get("--background")).save(args[2])
    elif mode == "layout":
        layout = json.load(open(args[1], encoding="utf-8"))
        fl = opts.get("--floor")
        draw_layout(layout, ppm, int(fl) if fl is not None else None).save(args[2])
    elif mode == "unskew":
        print(unskew(args[1], args[2], args[3]))
    else:
        print(__doc__)
    print("wrote", args[-1] if mode != "unskew" else args[3])


if __name__ == "__main__":
    main()
