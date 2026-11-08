"""Weapon pipeline: assemble the review cells (blender_weapon_review.py) into labelled before / after sheets.

    python Scripts/Pipeline/weapon_review_sheet.py <ArtPipeline/Weapons/Asset>      (system Python with Pillow)

Writes into 06_Review/: <Asset>_review_main.png (main part: side / top / 3-4, before | after),
<Asset>_review_parts.png (every other part, before | after, with triangle counts), <Asset>_atlas.png (the three maps +
the island layout) and <Asset>_topdown_scale.png (the main part as it looks at a tactical-camera size, before | after).
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def font(size):
    for p in ("C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def label(img, text, size=22):
    d = ImageDraw.Draw(img)
    f = font(size)
    w = d.textlength(text, font=f)
    d.rectangle([0, 0, w + 16, size + 12], fill=(0, 0, 0))
    d.text((8, 4), text, fill=(255, 255, 255), font=f)
    return img


def main():
    root = os.path.abspath(sys.argv[1])
    asset = os.path.basename(root)
    review = os.path.join(root, "06_Review")
    cells = os.path.join(review, "cells")
    cfg = json.load(open(os.path.join(root, "weapon.json")))
    summ = json.load(open(os.path.join(review, "summary.json")))
    red = summ.get("reduce", {}).get("parts", {})
    main_part = cfg.get("main_part", "")

    def cell(part, view, which):
        p = os.path.join(cells, "%s__%s__%s.png" % (part, view, which))
        return Image.open(p).convert("RGB") if os.path.exists(p) else None

    # main sheet
    rows = []
    for view in ("side", "top", "front34"):
        b, a = cell(main_part, view, "before"), cell(main_part, view, "after")
        if b and a:
            rows.append((view, b, a))
    if rows:
        w, h = rows[0][1].size
        sheet = Image.new("RGB", (w * 2, h * len(rows)))
        for i, (view, b, a) in enumerate(rows):
            sheet.paste(label(b.copy(), "BEFORE %s  %s  %d tris, 4K PBR set" % (main_part, view, red.get(main_part, {}).get("tris_before", 0))), (0, i * h))
            sheet.paste(label(a.copy(), "AFTER  %s  %s  %d tris, 1K atlas" % (main_part, view, red.get(main_part, {}).get("tris_after", 0))), (w, i * h))
        sheet.save(os.path.join(review, "%s_review_main.png" % asset))
        # tactical-camera scale: the top view at ~ 1/4 size, upscaled nearest for visibility
        b, a = rows[1][1], rows[1][2]
        small = (w // 3, h // 3)
        ts = Image.new("RGB", (small[0] * 2 + 10, small[1]), (20, 20, 20))
        ts.paste(b.resize(small, Image.LANCZOS), (0, 0))
        ts.paste(a.resize(small, Image.LANCZOS), (small[0] + 10, 0))
        ts = ts.resize((ts.width * 2, ts.height * 2), Image.NEAREST)
        ts.save(os.path.join(review, "%s_topdown_scale.png" % asset))

    # other parts
    others = [p for p in cfg["parts"] if p != main_part and cell(p, "front34", "after")]
    if others:
        b0 = cell(others[0], "front34", "before")
        w, h = b0.size
        cols = 2
        nrows = (len(others) + cols - 1) // cols
        sheet = Image.new("RGB", (w * 2 * cols, h * nrows), (20, 20, 20))
        for i, p in enumerate(others):
            x, y = (i % cols) * w * 2, (i // cols) * h
            b, a = cell(p, "front34", "before"), cell(p, "front34", "after")
            r = red.get(p, {})
            sheet.paste(label(b, "BEFORE %s  %d tris" % (p, r.get("tris_before", 0)), 20), (x, y))
            sheet.paste(label(a, "AFTER %s  %d tris" % (p, r.get("tris_after", 0)), 20), (x + w, y))
        sheet.save(os.path.join(review, "%s_review_parts.png" % asset))

    # atlas
    maps = []
    for k, nm in (("BaseColor", "BaseColor (sRGB)"), ("Normal", "Normal (DirectX)"), ("ORM", "ORM (R=AO G=Rough B=Metal)")):
        p = os.path.join(root, "04_Textures_Baked", "T_%s_%s.png" % (asset, k))
        if os.path.exists(p):
            maps.append((nm, Image.open(p).convert("RGB").resize((512, 512), Image.LANCZOS)))
    lay = os.path.join(review, "atlas_layout.png")
    if os.path.exists(lay):
        maps.append(("Atlas layout (colour = part)", Image.open(lay).convert("RGB").resize((512, 512), Image.NEAREST)))
    if maps:
        sheet = Image.new("RGB", (512 * len(maps), 512))
        for i, (nm, im) in enumerate(maps):
            sheet.paste(label(im, nm, 18), (i * 512, 0))
        sheet.save(os.path.join(review, "%s_atlas.png" % asset))
    print("sheets written to", review)


if __name__ == "__main__":
    main()
