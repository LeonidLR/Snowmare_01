"""Imports the formation radial menu icons (stage 7, hold B; docs/design/FORMATION_CONTRACT.md 9).

Source: Content/UI/FormationMenu/src/<item|auto>.svg (64 x 64, currentColor; generated from the rules by the Training Ground,
`npm run menu:icons`). UE 5.8 has no SVG texture importer, so this script rasterises them itself (pure Python, no PIL: the SVGs
use only <line>, <circle> (filled, stroked, dashed) and <path> with M / h / v / l, round caps) into white-on-alpha PNGs of
SIZE x SIZE (analytic anti-aliasing from signed distances), writes them to Saved/FormationMenuIcons/, and - inside the editor -
imports them as /Game/UI/FormationMenu/T_FormationMenu_<id> (UserInterface2D, no mips, UI group). The widget tints them
(UFormationMenuWidget).

Run headless (editor closed for this project, after build.ps1):
  UnrealEditor-Cmd.exe <abs path>/CodexTactics.uproject -run=pythonscript -script="<abs path to this file>" -unattended -nullrhi
Outside Unreal (`python Scripts/Editor/import_formation_menu_icons.py`) it only writes the PNGs (a preview / a check).
Result: Saved/Logs/import_formation_menu_icons.txt
"""
import math
import os
import re
import struct
import xml.etree.ElementTree as ET
import zlib

try:
    import unreal  # noqa: F401
except ImportError:
    unreal = None

SIZE = 128
IDS = ["line", "diamond", "echelon_right", "blizzard", "column", "hedgehog", "echelon_left", "all_forward", "auto"]
DEST = "/Game/UI/FormationMenu"

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, "..", ".."))
SRC = os.path.join(PROJECT, "Content", "UI", "FormationMenu", "src")
OUT = os.path.join(PROJECT, "Saved", "FormationMenuIcons")
LOG = os.path.join(PROJECT, "Saved", "Logs", "import_formation_menu_icons.txt")
log_lines = []


def note(text):
    log_lines.append(text)
    if unreal:
        unreal.log(text)
    else:
        print(text)


# --- shapes: each returns the signed distance (svg units, < 0 inside) at a point ---------------------------------------------

def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    length2 = dx * dx + dy * dy
    t = 0.0 if length2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / length2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


class Segment:
    def __init__(self, ax, ay, bx, by, width):
        self.a, self.b, self.half = (ax, ay), (bx, by), width / 2.0

    def sd(self, px, py):
        return seg_dist(px, py, self.a[0], self.a[1], self.b[0], self.b[1]) - self.half


class Disc:
    def __init__(self, cx, cy, r):
        self.c, self.r = (cx, cy), r

    def sd(self, px, py):
        return math.hypot(px - self.c[0], py - self.c[1]) - self.r


class Ring:
    """A stroked circle, optionally dashed (SVG: the dash starts at 3 o'clock and runs with the angle, y down), round caps."""

    def __init__(self, cx, cy, r, width, dashes):
        self.c, self.r, self.half, self.dashes = (cx, cy), r, width / 2.0, dashes

    def point(self, arc):
        angle = arc / self.r
        return self.c[0] + self.r * math.cos(angle), self.c[1] + self.r * math.sin(angle)

    def sd(self, px, py):
        dx, dy = px - self.c[0], py - self.c[1]
        radial = abs(math.hypot(dx, dy) - self.r) - self.half
        if not self.dashes:
            return radial
        period = sum(self.dashes)
        on = self.dashes[0]
        arc = (math.atan2(dy, dx) % (2 * math.pi)) * self.r
        pos = arc % period
        if pos <= on:
            return radial
        start = arc - pos
        ends = [self.point(start + on), self.point(start + period)]
        return min(math.hypot(px - x, py - y) for x, y in ends) - self.half


def num(element, name, default=0.0):
    value = element.get(name)
    return float(value) if value is not None else default


def parse_path(d, width):
    """M x y absolute, then relative h / v / l (and absolute H / V / L / M) - all the generator writes."""
    tokens = re.findall(r"[MmLlHhVv]|-?\d*\.?\d+(?:e-?\d+)?", d)
    shapes, x, y, cmd, i = [], 0.0, 0.0, None, 0
    while i < len(tokens):
        token = tokens[i]
        if re.match(r"[A-Za-z]", token):
            cmd = token
            i += 1
            continue
        if cmd in ("M", "m"):
            nx, ny = float(tokens[i]), float(tokens[i + 1])
            x, y = (nx, ny) if cmd == "M" else (x + nx, y + ny)
            i += 2
            cmd = "L" if cmd == "M" else "l"
            continue
        if cmd in ("L", "l"):
            nx, ny = float(tokens[i]), float(tokens[i + 1])
            tx, ty = (nx, ny) if cmd == "L" else (x + nx, y + ny)
            i += 2
        elif cmd in ("H", "h"):
            v = float(tokens[i])
            tx, ty = (v if cmd == "H" else x + v), y
            i += 1
        elif cmd in ("V", "v"):
            v = float(tokens[i])
            tx, ty = x, (v if cmd == "V" else y + v)
            i += 1
        else:
            raise ValueError("unsupported path command %r in %r" % (cmd, d))
        shapes.append(Segment(x, y, tx, ty, width))
        x, y = tx, ty
    return shapes


def load_shapes(svg_path):
    root = ET.parse(svg_path).getroot()
    view = [float(v) for v in root.get("viewBox", "0 0 64 64").split()]
    shapes = []
    for element in root.iter():
        tag = element.tag.split("}")[-1]
        width = num(element, "stroke-width", 1.0)
        if tag == "line":
            shapes.append(Segment(num(element, "x1"), num(element, "y1"), num(element, "x2"), num(element, "y2"), width))
        elif tag == "circle":
            cx, cy, r = num(element, "cx"), num(element, "cy"), num(element, "r")
            if element.get("fill") == "currentColor":
                shapes.append(Disc(cx, cy, r))
            if element.get("stroke") != "none":
                dash = element.get("stroke-dasharray")
                shapes.append(Ring(cx, cy, r, width, [float(v) for v in dash.split()] if dash else None))
        elif tag == "path":
            shapes.extend(parse_path(element.get("d", ""), width))
        elif tag not in ("svg",):
            raise ValueError("unsupported SVG element <%s> in %s" % (tag, svg_path))
    return view, shapes


def rasterise(svg_path, size):
    view, shapes = load_shapes(svg_path)
    scale = size / view[2]
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            sx, sy = view[0] + (px + 0.5) / scale, view[1] + (py + 0.5) / scale
            cover = 0.0
            for shape in shapes:
                cover = max(cover, min(1.0, max(0.0, 0.5 - shape.sd(sx, sy) * scale)))
                if cover >= 1.0:
                    break
            row += bytes((255, 255, 255, int(round(cover * 255))))
        rows.append(bytes(row))
    return rows


def write_png(path, rows, size):
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def import_textures(pngs):
    tasks = []
    for icon_id, png in pngs:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", png)
        task.set_editor_property("destination_path", DEST)
        task.set_editor_property("destination_name", "T_FormationMenu_" + icon_id)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for icon_id, _ in pngs:
        path = "%s/T_FormationMenu_%s" % (DEST, icon_id)
        texture = unreal.EditorAssetLibrary.load_asset(path)
        if not texture:
            note("FAIL: %s not imported" % path)
            continue
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("never_stream", True)
        unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
        note("imported %s" % path)


def main():
    os.makedirs(OUT, exist_ok=True)
    pngs = []
    for icon_id in IDS:
        svg = os.path.join(SRC, icon_id + ".svg")
        rows = rasterise(svg, SIZE)
        png = os.path.join(OUT, icon_id + ".png")
        write_png(png, rows, SIZE)
        covered = sum(1 for row in rows for i in range(3, len(row), 4) if row[i] > 0)
        note("rasterised %s -> %s (%d px covered)" % (svg, png, covered))
        pngs.append((icon_id, png))
    if unreal:
        import_textures(pngs)
    else:
        note("not inside Unreal: PNGs only")
    note("RESULT: %d icons" % len(pngs))


try:
    main()
except Exception as error:  # the log file is the only output of a commandlet
    note("FAIL: %r" % (error,))
    raise
finally:
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    with open(LOG, "w", encoding="utf-8") as handle:
        handle.write("\n".join(log_lines) + "\n")
