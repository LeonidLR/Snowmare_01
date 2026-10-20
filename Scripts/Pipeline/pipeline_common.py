"""Shared helpers of the character single-slot pipeline (Blender side).

Used by blender_clean.py / blender_atlas.py / blender_bake.py / blender_export.py / blender_review.py, all run as
    blender --background --factory-startup --python Scripts/Pipeline/<script>.py -- --asset-dir <ArtPipeline asset folder> [...]
Docs: docs/pipeline/CHARACTER_SINGLE_SLOT.md
"""
import json
import math
import os
import re
import sys
import time

import bpy

# Unreal works in centimetres. With the scene unit scale at 0.01 the FBX importer keeps the data in centimetres and
# every object at scale 1 - the round trip back to Unreal then needs no scale fix-ups.
SCENE_UNIT_SCALE = 0.01

# Texture name suffix -> role. Only a fallback / cross-check: the material graph in inventory.json is the truth.
NAME_ROLE_PATTERNS = [
    (r"(_BaseColor|_Albedo|_Diffuse|_BC|_D)$", "base_color"),
    (r"(_Normal|_Nrm|_N)$", "normal"),
    (r"(_OcclusionRoughnessMetallic|_ORM)$", "orm_packed"),
    (r"(_Roughness|_R)$", "roughness"),
    (r"(_Metallic|_Metalness|_M)$", "metallic"),
    (r"(_AmbientOcclusion|_AO|_Occlusion)$", "ambient_occlusion"),
    (r"(_Emissive|_Emission|_E)$", "emissive"),
    (r"(_Opacity|_Alpha|_Mask)$", "opacity"),
]

# No .blend1 backups next to the working files (every stage re-creates its .blend from the previous stage).
bpy.context.preferences.filepaths.save_version = 0

HAIR_REGEX_DEFAULT = (r"hair|(^|_)fur($|_)|eyebrow|(^|_)brows?($|_)|eyelash|(^|_)lashes($|_)|beard|mustache|"
                      r"moustache|groom")  # note: plain "lash" would match "Flashlight"


def script_args():
    """Arguments after '--' on the Blender command line."""
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


class Paths:
    """Folder layout of one ArtPipeline asset (see ArtPipeline/README.md)."""

    def __init__(self, asset_dir, name=None):
        self.root = os.path.abspath(asset_dir)
        self.source = os.path.join(self.root, "01_Source")
        self.tex_source = os.path.join(self.root, "02_Textures_Source")
        self.blend = os.path.join(self.root, "03_Blender")
        self.baked = os.path.join(self.root, "04_Textures_Baked")
        self.export = os.path.join(self.root, "05_Export")
        self.review = os.path.join(self.root, "06_Review")
        for p in (self.source, self.tex_source, self.blend, self.baked, self.export, self.review):
            os.makedirs(p, exist_ok=True)
        self.inventory_path = os.path.join(self.source, "inventory.json")
        self.inventory = load_json(self.inventory_path)
        self.mesh_name = name or self.inventory["name"]
        # "SK_Post_Apo_Survivor_a" -> "Post_Apo_Survivor_a" for texture names
        self.short = re.sub(r"^SK_", "", self.mesh_name)
        self.source_fbx = os.path.join(self.root, self.inventory["exports"]["mesh_fbx"])
        self.clean_blend = os.path.join(self.blend, self.mesh_name + "_clean.blend")
        self.reduce_blend = os.path.join(self.blend, self.mesh_name + "_reduce.blend")
        self.atlas_blend = os.path.join(self.blend, self.mesh_name + "_atlas.blend")
        self.bake_blend = os.path.join(self.blend, self.mesh_name + "_bake.blend")
        self.original_blend = os.path.join(self.blend, self.mesh_name + "_original.blend")
        self.export_fbx = os.path.join(self.export, self.mesh_name + "_1slot.fbx")
        self.report_path = os.path.join(self.review, "summary.json")

    @property
    def high_name(self):
        """The bake source kept next to a reduced mesh (blender_reduce.py)."""
        return self.mesh_name + "_high"

    def tex(self, kind, ext="png"):
        return os.path.join(self.baked, "T_%s_%s.%s" % (self.short, kind, ext))


def load_json(path, default=None):
    if not os.path.exists(path):
        return {} if default is None else default
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path, data):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)


def update_summary(paths, section, data):
    """Merge one stage's numbers into 06_Review/summary.json (the QA record of the run)."""
    summary = load_json(paths.report_path)
    summary.setdefault("asset", paths.mesh_name)
    summary[section] = data
    summary.setdefault("stage_times", {})[section] = time.strftime("%Y-%m-%d %H:%M:%S")
    save_json(paths.report_path, summary)


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = SCENE_UNIT_SCALE


def import_fbx(path, with_anim=False):
    """Import an Unreal FBX in centimetres. Returns (armature, [meshes], [other new objects])."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, use_anim=with_anim, ignore_leaf_bones=False,
                             automatic_bone_orientation=False, use_custom_normals=True)
    new = [o for o in bpy.data.objects if o not in before]
    arm = next((o for o in new if o.type == "ARMATURE"), None)
    meshes = [o for o in new if o.type == "MESH"]
    others = [o for o in new if o.type not in ("ARMATURE", "MESH")]
    return arm, meshes, others


def import_unreal_mesh(paths):
    """Import the exported skeletal mesh; drop the stray top Null; name the mesh after the asset."""
    arm, meshes, others = import_fbx(paths.source_fbx)
    if arm is None or len(meshes) != 1:
        raise RuntimeError("Expected one armature + one mesh in %s, got %s / %d meshes" % (
            paths.source_fbx, arm, len(meshes)))
    for o in others:  # the exporter's top Null named after the mesh
        for child in list(o.children):
            mw = child.matrix_world.copy()
            child.parent = None
            child.matrix_world = mw
        bpy.data.objects.remove(o)
    # The FBX references the vendor's original texture paths (E:\...): drop those images, the materials are
    # rebuilt from the exported PNGs.
    for img in list(bpy.data.images):
        bpy.data.images.remove(img)
    mesh = meshes[0]
    mesh.name = paths.mesh_name
    mesh.data.name = paths.mesh_name
    return arm, mesh


def enable_uvpackmaster():
    """UVPackmaster 4 draws with the gpu module at import time: initialise it first (works with --background)."""
    import gpu
    import addon_utils
    if hasattr(gpu, "init"):
        try:
            gpu.init()
        except Exception as exc:  # already initialised / no GPU
            print("gpu.init:", exc)
    mod = addon_utils.enable("bl_ext.user_default.uvpackmaster4", default_set=True)
    return mod is not None and hasattr(bpy.context.scene, "uvpm4_props")


def setup_cycles(samples=4, device="GPU"):
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


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def fibonacci_sphere(n):
    """n roughly uniform unit directions."""
    out = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(n):
        y = 1.0 - (i + 0.5) * 2.0 / n
        r = math.sqrt(max(0.0, 1.0 - y * y))
        t = golden * i
        out.append((math.cos(t) * r, y, math.sin(t) * r))
    return out


def write_png(path, rgb, bits=8):
    """Minimal PNG writer (numpy array HxWx3 or HxW, values 0..1, top row first). 8 or 16 bit."""
    import struct
    import zlib
    import numpy as np
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
        f.write(bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]))  # PNG signature
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bits, color_type, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw.tobytes(), 6)))
        f.write(chunk(b"IEND", b""))


def read_png_rgb(path):
    """Load a PNG through Blender into a float numpy array (HxWx4, top row first, raw encoded values)."""
    import numpy as np
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = "Non-Color"
    a = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    out = a.reshape(img.size[1], img.size[0], 4)[::-1]
    bpy.data.images.remove(img)
    return out


class Timer:
    def __init__(self, label):
        self.label = label

    def __enter__(self):
        self.t = time.time()
        print("[pipeline] %s ..." % self.label, flush=True)
        return self

    def __exit__(self, *exc):
        self.seconds = time.time() - self.t
        print("[pipeline] %s done in %.1fs" % (self.label, self.seconds), flush=True)
