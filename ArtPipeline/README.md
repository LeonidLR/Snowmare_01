# ArtPipeline — DCC working area (outside Unreal's Content)

One folder per asset; Unreal never imports from here directly, the pipeline scripts do.

```
ArtPipeline/
  Characters/
    Operatives/<Character>/   Enemies/<Character>/
  Props/<Prop>/   Weapons/<Weapon>/   Environment/<Set>/   VFX/<Effect>/
  _Scripts/            (pipeline scripts copied here for reference; the source of truth is Scripts/Pipeline/)
```

Inside every asset folder:

| Folder | What |
|---|---|
| `01_Source/` | what came out of Unreal: FBX / OBJ export, `inventory.json` (slots, materials, textures, triangles) |
| `02_Textures_Source/` | the original textures exported from Unreal (PNG / EXR) |
| `03_Blender/` | Blender working files (`.blend`): clean-up, atlas UV, bake setup |
| `04_Textures_Baked/` | the baked atlas: `T_<Name>_BaseColor`, `_Normal`, `_ORM` (+ previews) |
| `05_Export/` | the final FBX that goes back into Unreal |
| `06_Review/` | before / after screenshots, draw-call numbers, notes |

Binary files here are NOT tracked by git (see `.gitignore`); `README.md` and `*.json` manifests are.
Resolutions (designer 2026-10-20): operatives 4K atlas, enemies 2K; ORM at half the atlas size.
