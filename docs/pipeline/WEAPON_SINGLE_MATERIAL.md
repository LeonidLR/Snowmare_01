# Weapon single-material pipeline (2026-11-08)

Same principles as the character single-slot pipeline (`CHARACTER_SINGLE_SLOT.md`): top-down tactical camera, fewer
triangles, ONE material per weapon with ONE small texture set. Non-destructive: the Fab packs are read-only, the results
live in `ArtPipeline/Weapons/<Asset>/` (the "Optimized" source package) and are NOT imported into the game by this
pipeline (another task imports / wires them).

Budgets (proposal of the 2026-11-08 task, the designer may adjust them in `weapon.json`):

| Asset | Parts | Budget |
|---|---|---|
| Pistol 9mm Custom (Fab, DTry) | pistol, magazine, bullet, 11 attachments | pistol ~3 000 tris, magazine ~700, bullet ~120, every attachment 600-1 400; ONE 1K set for the whole kit |
| Shotgun D-1 (Fab import `Content/Fab/_Model_D-1__Shotgun`) | gun body (skinned) + pump (skinned, own skeleton) | ~5 000 tris in total; ONE 1K set |

Textures: `T_<Asset>_BaseColor` (sRGB, 8-bit), `T_<Asset>_Normal` (DirectX / green down, 16-bit PNG), `T_<Asset>_ORM`
(linear 8-bit: R = ambient occlusion, G = roughness, B = metallic). No emissive map (see the asset READMEs for what glows).

## Tools and stages

| Stage | Script | What |
|---|---|---|
| 0 | `run_unreal_export_weapon.ps1` -> `unreal_export_weapon.py` | headless read-only Unreal export of a Fab pack (FBX + inventory.json); the pistol starts from the pack's FBX instead |
| 1 prep | `blender_weapon_prep.py` | high parts in weapon space, centimetres; pistol: attachments moved from the pack's display row to their mounted position |
| 2 reduce | `blender_weapon_reduce.py` | weld, collapse-decimate each part to its budget (small pieces / uv seams / bone joints shielded by a vertex group), normals = smooth + sharp edges > 45 degrees, the artist's uv islands are repacked into one atlas uv (`UV_Atlas`, one tile per part before the pack, per-part texel-density weight) |
| 3 bake | `blender_weapon_bake.py` | Cycles selected-to-active projection from each part's own high objects (4K source textures rebuilt as materials, loaded per part): BaseColor / roughness / metallic / AO as emission passes, tangent-space normal (includes the source normal map), ray-miss pass, 6 px dilation |
| 4 export | `blender_weapon_export.py` | one FBX per part with one material slot `M_<Asset>`; round trip checked (one mesh, one slot, one uv map, triangle count, bones) |
| 5 review | `blender_weapon_review.py` + `weapon_review_sheet.py` | Cycles before (high, original 4K PBR) / after (reduced, atlas) cells and labelled sheets |
| check | `unreal_check_weapon_import.py` | import of the FBX into a throw-away scratch project: scale, pivot, axis, slots (never the game project) |
| clean | `cleanup_intermediates.ps1 -NoImportCheck -KeepReviewSheets` | deletes .blend files, logs, source FBX / textures, review cells; keeps the final maps, the export FBX, json / md and the review sheets |

Run: `powershell -ExecutionPolicy Bypass -File Scripts/Pipeline/run_weapon_pipeline.ps1 -AssetDir ArtPipeline/Weapons/Pistol9mm`
(`-Stages bake`, `-Only Part` for one stage / part). Everything is parameterised by `ArtPipeline/Weapons/<Asset>/weapon.json`
(budgets, texel-density weights, pack-material -> texture mapping, resolution).

## Traps found (2026-11-08)

- `merge_overlap` in the pack: the artist's sheets of all parts live in the same 0..1 tile; without moving every part to its
  own tile first, Blender treats them as overlapping and stacks different parts onto the same texels.
- A uv unwrap of the decimated mesh (Smart UV Project) gives ~2 000 tiny islands and 5 % usable atlas: keep the artist's islands
  (decimation with a vertex group that shields the uv seams) and repack them.
- Paths with 8.3 short names (`C:\Users\NAME~1\...`) crash UnrealEditor-Cmd (`FilenameToLongPackageName`); a worktree without
  `Binaries` cannot start the commandlet: use a content-only scratch project that holds a COPY of the pack.
- The Fab shotgun's pump (`Object_29`) is a skeletal mesh whose vertices are 625x too big and whose single bone is scaled
  625x (a Sketchfab glTF scale trick): the pump geometry / skeleton are never changed; only its uv map is re-pointed.
- The Fab shotgun materials are flat colours (gltf factors, no textures): the atlas of the gun is a baked colour + normal
  atlas, the pump takes a flat swatch of it.
