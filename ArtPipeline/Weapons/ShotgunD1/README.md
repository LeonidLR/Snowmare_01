# Shotgun D-1 - optimized source package (2026-11-08)

Source: the user's Fab import `Content/Fab/_Model_D-1__Shotgun/` (read-only; exported with
`Scripts/Pipeline/run_unreal_export_weapon.ps1`, nothing in the pack was modified or saved). Pipeline:
`docs/pipeline/WEAPON_SINGLE_MATERIAL.md`. **Not imported into the game** - this folder is the package for the importer.

## Result

| Mesh (FBX) | Source asset | Triangles before | after | Slots before | after |
|---|---|---|---|---|---|
| `05_Export/SK_ShotgunD1_1slot.fbx` | `Stock_Plastic_-_Matte__Dark_Gray__0` (the gun: stock, grip, receiver, barrel, heat shield, magazine, clamp) | 18 226 | **4 500** | 7 | **1** |
| `05_Export/SK_ShotgunD1_Pump_1slot.fbx` | `Object_29` (the pump / forend) | 496 | **496** (geometry untouched) | 1 | **1** |
| total | | 18 722 | **4 996** (budget ~5 000) | 8 | 1 material `M_ShotgunD1` |

Textures (`04_Textures_Baked/`, ONE 1K set for both meshes): `T_ShotgunD1_BaseColor.png` (sRGB), `T_ShotgunD1_Normal.png`
(DirectX / green down, 16-bit), `T_ShotgunD1_ORM.png` (linear: R = AO, G = roughness, B = metallic). 3 textures instead of 7
material slots (the source materials are flat colours, no textures at all; the 1K set is a baked colour + normal atlas).

Measured with the baked atlas: texel density 15.8 px/cm (real centimetres) on the gun, atlas coverage 61 %, ray misses 0.07 %,
3.3 % of the normal texels clamped (rays that reached a back face). Before / after renders: `06_Review/ShotgunD1_review_main.png`
(side / top / 3-4, before = original materials, after = reduced mesh + atlas), `ShotgunD1_atlas.png`, `ShotgunD1_topdown_scale.png`.

## Import notes

- **Skeletons / hierarchy are the original ones.** `SK_ShotgunD1_1slot.fbx` carries the same 31 bones / node names as the
  original export (armature object `Sketchfab_Scene`, bones `Sketchfab_model ... Weapon_00 -> Pump_01 -> Pump_end_02`,
  `Stock`, `Handle`, `Reciever`, `Heatshield`, `MagClamp`, `Mag`, `Barrel` ...). Import it onto the existing skeleton
  `/Game/Fab/_Model_D-1__Shotgun/model_d00m_1_shotgun/SkeletalMeshes/Stock_Plastic_-_Matte__Dark_Gray__0_Skeleton`, the pump onto
  `.../Pump_01_Skeleton`. The 8 `model_d00m_1_shotgunArmature_*` animations keep working (no bone was added / removed / renamed).
  The bone weights are rigid like the source (every vertex = ONE bone, weight 1); the decimation shielded the joints between
  rigid pieces.
- **Scale / pivot / axes are exactly the original's** (checked by importing original and new FBX into a throw-away scratch
  project with the same skeleton: bounds origin / extent identical within 0.04 %: gun origin (0, -442, -542) extent
  (255, 4025, 853) vs the original (0, -442, -539) (250, 4024, 850)). NOTE: the Fab mesh is authored at 100x its real size
  (Sketchfab glTF scale: the gun is 80 m long in the mesh asset, the pump mesh is 625x too big with its bone scaled 625x); the
  animations / component transform compensate. Use the same import settings as the original (uniform scale 1, no axis change)
  and the same component setup; nothing about this was changed.
- **Material:** both meshes have ONE slot named `M_ShotgunD1`. Create one material (instance) `M_ShotgunD1`: BaseColor sRGB ->
  Base Color, Normal (TC_Normalmap, sRGB off, no Flip Green: it is DirectX) -> Normal, ORM (TC_Masks, sRGB off) R -> AO,
  G -> Roughness, B -> Metallic. Opaque, default lit. The ORM values are FINAL (do not apply Power / sRGB).
- **Pump:** geometry, skin and bone are untouched; only its UV map was changed - all its UVs point at one flat swatch of the atlas
  (u 0.6125, v 0.9782 in Blender's bottom-up v; 13x13 px) holding the pump's plastic colour. Its 496 tris keep their normals.
- **LODs:** none in the FBX; generate in Unreal (e.g. 50 % / 25 %) if wanted. Collision / physics assets: the originals
  (`Object_29_PhysicsAsset`, `Stock_..._PhysicsAsset`) are untouched and still fit (bounds unchanged).

## What glows (emissive) - proposal for the designer

The source has two emissive materials (`Emmissive_Green` 6 tris on the receiver, `Emmissive_Red` 112 tris on the magazine
clamp, EmissiveStrength 3.0). Default as agreed: **no emissive texture**; the two LEDs are baked as saturated pure green / pure red
colour into the BaseColor (they read correctly but do not glow). Proposal if the glow is wanted: either (a) a scalar `Emissive`
parameter on the material multiplied by a mask taken from the BaseColor (pure red / green texels), or (b) the BaseColor alpha as
an emissive mask (the only change would be an RGBA BaseColor, BC7). The LEDs are under 0.2 % of the surface, so an emissive
texture is not worth its memory; the cheap option (a) can come later.

## Files

`weapon.json` (budgets, material table - the flat colours come from the Unreal material instances: BaseColorFactor /
RoughnessFactor / MetallicFactor, default metallic 1.0 for the steel / titanium instances that do not override it),
`01_Source/inventory.json` (slots / materials / bounds of the Unreal export), `05_Export/export_manifest.json`
(round-trip checks: one mesh, one slot, one uv map, triangle counts, bone lists), `06_Review/summary.json` (every stage's numbers).
Intermediates (`.blend`, logs, source FBX, review cells) are deleted by `cleanup_intermediates.ps1`; re-run from the scripts.
