"""The RPG-7's 1K texture set from the launcher pack's 4K maps (system Python + Pillow + numpy; 2026-11-09, feat/rpg7-launcher-pack).

    python Scripts/Pipeline/rpg7_textures.py ArtPipeline/Weapons/RPG7

The pack's M_Launcher (read from Unreal): BaseColor = Albedo.RGB, Metallic = Metallic.RGB, Roughness = Metallic.A, AO = Ao, Normal = Normal.RGB;
Albedo / Metallic / Ao are sRGB textures, so the shader saw sRGB-decoded values in every channel. The weapon pipeline's set
(docs/pipeline/WEAPON_SINGLE_MATERIAL.md) is BaseColor (sRGB) + Normal (DirectX, as the pack's - Unreal convention) + ORM (LINEAR: R = AO,
G = roughness, B = metallic), so the ORM channels are the decoded values. Every part keeps the pack's uv islands (blender_weapon_rpg7.py), so a
resample is all it takes (Lanczos, 4096 -> res). Writes 04_Textures_Baked/T_<Asset>_{BaseColor,Normal,ORM}.png and merges the numbers into
06_Review/summary.json.
"""
import json
import os
import sys

import numpy as np
from PIL import Image


def srgb_to_linear(a):
    a = a.astype(np.float32) / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def main(root):
    cfg = json.load(open(os.path.join(root, "weapon.json"), encoding="utf-8"))
    asset, res = cfg["asset"], int(cfg.get("res", 1024))
    src = os.path.join(root, "02_Textures_Source")
    out = os.path.join(root, "04_Textures_Baked")
    os.makedirs(out, exist_ok=True)
    size = (res, res)
    albedo = Image.open(os.path.join(src, "T_Launcher_Weapon_Albedo.png")).convert("RGB").resize(size, Image.LANCZOS)
    albedo.save(os.path.join(out, f"T_{asset}_BaseColor.png"))
    normal = Image.open(os.path.join(src, "T_Launcher_Weapon_Normal.png")).convert("RGB").resize(size, Image.LANCZOS)
    n = np.asarray(normal).astype(np.float32) / 255.0 * 2.0 - 1.0
    n /= np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-6)  # renormalise after the filter
    Image.fromarray(np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGB").save(os.path.join(out, f"T_{asset}_Normal.png"))
    metal = Image.open(os.path.join(src, "T_Launcher_Weapon_Metallic.png")).convert("RGBA").resize(size, Image.LANCZOS)
    ao = Image.open(os.path.join(src, "T_Launcher_Weapon_Ao.png")).convert("L").resize(size, Image.LANCZOS)
    m = np.asarray(metal)
    orm = np.stack([srgb_to_linear(np.asarray(ao)), srgb_to_linear(m[..., 3]), srgb_to_linear(m[..., 0])], axis=2)
    Image.fromarray(np.clip(orm * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGB").save(os.path.join(out, f"T_{asset}_ORM.png"))
    stats = {k: os.path.getsize(os.path.join(out, f"T_{asset}_{k}.png")) for k in ("BaseColor", "Normal", "ORM")}
    src_bytes = {f: os.path.getsize(os.path.join(src, f)) for f in os.listdir(src) if f.endswith(".png")}
    sp = os.path.join(root, "06_Review", "summary.json")
    os.makedirs(os.path.dirname(sp), exist_ok=True)
    summary = json.load(open(sp, encoding="utf-8")) if os.path.exists(sp) else {}
    summary["textures"] = {"res": res, "out_png_bytes": stats, "source_png_bytes": src_bytes, "source_res": 4096,
                           "orm_means": [round(float(orm[..., i].mean()), 3) for i in range(3)]}
    json.dump(summary, open(sp, "w"), indent=2)
    print("RPG7_TEXTURES_DONE", stats)


if __name__ == "__main__":
    main(os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "ArtPipeline/Weapons/RPG7"))
