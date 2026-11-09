"""Shared helpers of the weapon single-material pipeline (Blender side).

Used by blender_weapon_prep.py / blender_weapon_reduce.py / blender_weapon_bake.py / blender_weapon_export.py /
blender_weapon_review.py. Asset folder layout = ArtPipeline/Weapons/<Asset>/01_Source .. 06_Review.
Docs: docs/pipeline/WEAPON_SINGLE_MATERIAL.md
"""
import json
import os
import time

import bpy
import numpy as np

ATLAS_UV = "UV_Atlas"


class Timer:
    def __init__(self):
        self.t0 = time.time()

    def elapsed(self):
        return time.time() - self.t0


class Ctx:
    def __init__(self, asset_dir):
        self.root = os.path.abspath(asset_dir)
        self.asset = os.path.basename(self.root)
        self.source = os.path.join(self.root, "01_Source")
        self.tex_src = os.path.join(self.root, "02_Textures_Source")
        self.blend = os.path.join(self.root, "03_Blender")
        self.baked = os.path.join(self.root, "04_Textures_Baked")
        self.export = os.path.join(self.root, "05_Export")
        self.review = os.path.join(self.root, "06_Review")
        self.config = load_json(os.path.join(self.root, "weapon.json"), {})
        self.high_blend = os.path.join(self.blend, self.asset + "_high.blend")
        self.low_blend = os.path.join(self.blend, self.asset + "_low.blend")
        self.baked_blend = os.path.join(self.blend, self.asset + "_baked.blend")
        for d in (self.source, self.blend, self.baked, self.export, self.review):
            os.makedirs(d, exist_ok=True)

    def tex(self, kind):
        return os.path.join(self.baked, "T_%s_%s.png" % (self.asset, kind))


def load_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def update_summary(ctx, section, data):
    path = os.path.join(ctx.review, "summary.json")
    summary = load_json(path, {})
    summary.setdefault("asset", ctx.asset)
    summary[section] = data
    summary.setdefault("stage_times", {})[section] = time.strftime("%Y-%m-%d %H:%M:%S")
    save_json(path, summary)


def new_scene_cm():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 0.01
    return sc


def tri_count(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def setup_cycles(samples=8, device="GPU"):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    used = "CPU"
    if device == "GPU":
        prefs = bpy.context.preferences.addons["cycles"].preferences
        for backend in ("OPTIX", "CUDA", "HIP", "ONEAPI", "METAL"):
            try:
                prefs.compute_device_type = backend
                prefs.get_devices()
            except Exception:
                continue
            gpus = [d for d in prefs.devices if d.type == backend]
            if gpus:
                for d in prefs.devices:
                    d.use = d.type == backend
                scene.cycles.device = "GPU"
                used = backend
                break
    if used == "CPU":
        scene.cycles.device = "CPU"
    return used


def write_png(path, rgb, bits=8):
    """Minimal PNG writer (numpy HxWx{1,3,4} 0..1, top row first)."""
    import struct
    import zlib
    a = np.asarray(rgb, dtype=np.float32)
    if a.ndim == 2:
        a = a[..., None]
    h, w, c = a.shape
    color_type = {1: 0, 3: 2, 4: 6}[c]
    maxv = (1 << bits) - 1
    q = np.clip(np.round(a * maxv), 0, maxv).astype(">u2" if bits == 16 else np.uint8)
    raw = np.concatenate([np.zeros((h, 1), dtype=np.uint8), q.reshape(h, -1).view(np.uint8)], axis=1)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]))
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bits, color_type, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw.tobytes(), 6)))
        f.write(chunk(b"IEND", b""))


def dilate(rgb, mask, iterations):
    """Grow baked texels into empty space (8-neighbour average), like a bake margin."""
    rgb = rgb.copy()
    m = mask.copy()
    for _ in range(iterations):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(m.shape, dtype=np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == 0 and dx == 0:
                    continue
                ms = np.roll(np.roll(m, dy, axis=0), dx, axis=1)
                acc += np.roll(np.roll(rgb, dy, axis=0), dx, axis=1) * ms[..., None]
                cnt += ms
        grow = (~m) & (cnt > 0)
        if not grow.any():
            break
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        m |= grow
    return rgb
