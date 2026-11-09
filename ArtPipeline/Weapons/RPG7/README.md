# RPG-7 - optimized source package (2026-11-09, branch feat/rpg7-launcher-pack)

Source: the game designer's launcher pack `/Game/Launcher_Animations` (the user's untracked folder, reached through a junction; **read only - nothing
in it was modified, moved or re-saved**). Exported read-only by `Scripts/Pipeline/run_unreal_export_weapon.ps1 -Folder /Game/Launcher_Animations/Mesh
-Meshes SK_Weapon_` into `01_Source/` (6 skeletal meshes as FBX + `inventory.json`) and `02_Textures_Source/` (the four 4K maps).
Pipeline: `docs/pipeline/WEAPON_SINGLE_MATERIAL.md`, but simpler: the pack ALREADY has one material (`M_Launcher`) and one 4K texture set for
every part, so there is no re-atlas and no bake.

| Stage | Script | What |
|---|---|---|
| model | `Scripts/Pipeline/blender_weapon_rpg7.py` (Blender 5.2, background) | each part (weapon.json "parts") at its bind pose, welded, collapse-decimated to its budget with the artist's uv seams / open borders shielded (the islands survive, so the 1K maps still fit), smooth by angle 45 deg, ONE static FBX per part with ONE slot `M_RPG7`; frame found by fitting the imported boxes to the Unreal bounds (error 0.001 cm, scale 1) |
| textures | `Scripts/Pipeline/rpg7_textures.py` (Python + Pillow) | Lanczos 4096 -> 1024: `T_RPG7_BaseColor` (sRGB, = Albedo), `T_RPG7_Normal` (renormalised, Unreal convention as the pack's), `T_RPG7_ORM` (LINEAR: R = AO, G = roughness = the Metallic map's alpha, B = metallic; the pack's maps are sRGB textures, so the decoded values are stored) |
| import | `Scripts/Editor/import_rpg7.py` (commandlet) | `/Game/Weapons/RPG7/` (Body, Sight01, Rocket) + `Attachments/` (Sight02, Sight03), `Textures/`, `Materials/MI_RPG7` (child of `M_WeaponMaster`) |
| grip | `Scripts/Editor/calibrate_launcher_grip.py` (commandlet, read only) | the attach transform on the operative's `hand_r` from the retargeted shouldered idle |

Run: `python Scripts/Pipeline/rpg7_textures.py ArtPipeline/Weapons/RPG7` then
`blender --background --factory-startup --python Scripts/Pipeline/blender_weapon_rpg7.py -- --asset-dir ArtPipeline/Weapons/RPG7`, then the import
commandlet. The FBX / PNG are git-ignored (only this README and the json files are tracked).

## Result

| Part (FBX `05_Export/`) | Source meshes | Tris before | after | Use |
|---|---|---|---|---|
| `SM_RPG7_Body` | SK_Weapon_Body_Mesh | 9,369 | **5,000** | the tube, grips, trigger (budget ~5k like the D-1) |
| `SM_RPG7_Sight01` | SK_Weapon_Sight_01_Mesh + SK_Weapon_Glasses_Mesh (the lens) | 8,297 | **1,099** | the PGO-7 style optic on the game model (it reads best from the tactical camera: the only sight with a silhouette) |
| `SM_RPG7_Rocket` | SK_Weapon_Rocket_Mesh | 3,062 | **900** | the loaded rocket on the tube AND the flying projectile |
| `SM_RPG7_Sight02` | SK_Weapon_Sight_02_Mesh | 204 | **200** | iron front sight post - imported to `Attachments/`, unused (future customisation) |
| `SM_RPG7_Sight03` | SK_Weapon_Sight_03_Mesh | 676 | **498** | iron rear sight - imported to `Attachments/`, unused |
| **in the hand** (body + optic + rocket) | | **20,728** | **6,999** | |

Textures: 4 x 4K (Albedo / Ao / Metallic+roughness / Normal; 77 MB of PNG, the pack's uassets ~80 MB) -> 3 x 1K (`T_RPG7_*`, 5.5 MB of uassets).
The whole `/Game/Weapons/RPG7` folder: ~6.3 MB.  Review: `06_Review/RPG7_review_main.png` (workbench, texture colour; left = the pack's
high mesh with the 4K albedo, right = the reduced mesh with the 1K BaseColor; top row side, bottom row top view).

## Frame / mounting

- **The pack's weapon frame, unchanged** (SK_Weapon_Body_Mesh component space, cm): muzzle **+Y**, up **+Z**, the optic on **+X** (= the shooter's
  left). Every part is exported in its mounted place, so the optic and the rocket attach to the body at identity.
- The pack holds the tube on `SK_Mannequin`'s socket `Soket_Launcher` (hand_r, loc (0.06, 8.95, 14.1), rot P 2.06 Y 106.72 R 16.79). The operatives'
  hand_r frames differ from the pack mannequin's by ~10 deg after the IK retarget, so the attach is recomputed per body by
  `calibrate_launcher_grip.py`: the tube's orientation from the pack's idle (level, along the body's forward), its position keeping the right
  hand on the pistol grip. Male (the Commander / Engineer): **attach_location [-2.44, 8.78, 13.99], attach_rotation_pyr [-0.21, 108.04, 25.97]**
  (in `weapons_tuning.json` "rpg7.visual"); the Medic-Sapper's own would be [-2.96, 9.11, 13.68] / [-2.12, 110.10, 26.16] (with the male value
  her tube sits ~2.5 deg off). Residuals with the calibrated attach (male): the left hand <= 3.7 cm off its grip, the right shoulder 8.7 cm
  off the tube's axis (the pack: 9.6 - on the shoulder); female: left hand <= 6.6 cm (reload), shoulder 9.3-11.6 cm.
- Muzzle (the tube's front) `muzzle_offset` [0, 36, 0.3]; the back-blast point (its rear end) `backblast_offset` [0, -67, 0.3].
- The flying rocket: the same mesh turned yaw -90 (its +Y onto the flight's +X), its tip (y 76) at the round's point:
  `projectile_mesh_location` [-76, 0, -0.3], `projectile_mesh_rotation_pyr` [0, -90, 0].

## Emissive / effects

None in the pack. The muzzle flash / back-blast smoke / rocket trail are DATA HOOKS (`weapons_tuning.json` "rpg7.visual.fx": `muzzle_flash`,
`backblast`, `trail` - Niagara system paths, empty by default): the designer's look reference can be plugged in without code.
