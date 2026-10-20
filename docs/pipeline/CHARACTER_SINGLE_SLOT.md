# Character single-slot pipeline (draft, 2026-10-20)

Goal: every character / enemy skeletal mesh renders with **one material slot** (one draw call per pass) instead of
~12–14, without visible quality loss from the top-down camera. Hair (not needed under helmets / hoods) is removed.
Originals are never overwritten: every result is a new asset next to a copy, swapped in the Blueprint only after review.

## Tools

| Tool | Role | How it is driven |
|---|---|---|
| Unreal Editor 5.8 (commandlet `-run=pythonscript`) | export the mesh + textures + slot list; import the result; material instance; LODs; Blueprint swap; screenshots | editor Python scripts in `Scripts/Pipeline/` |
| Blender 5.2 (background `--background --python`) | clean-up (hair, hidden body parts), atlas UV, **bake** (Cycles), FBX export with UE-safe settings (no leaf bones, `Armature` object name — no extra root bone) | `Scripts/Pipeline/blender_*.py` |
| UVPackmaster 4 (Blender extension) | tight atlas packing with texel-density weights and island padding | called from the Blender script; built-in Blender packer as fallback |
| Marmoset Toolbag 3 | optional higher-quality bake (normals / AO) when Cycles shows artefacts | manual / script, only if needed |
| Substance Painter | optional manual polish of an atlas (seams, faces, decals) | manual, by the designer |
| Smokes / unit tests | in-game checks: slot count, no missing textures, no T-pose, screenshots | `CodexTactics.CharacterSlotsSmoke` (to write) |

Blender Cycles is the primary baker because it runs fully headless and reproducibly; Marmoset / Substance are for
hand-quality passes on characters that need it.

## Steps

1. **Inventory (Unreal, read-only)** — for each target mesh: slots, materials, their texture parameters (base colour,
   normal, ORM / roughness / metal / AO, opacity), texture sizes, triangle count, LODs, which Blueprints use it.
   Output: `Saved/Pipeline/<mesh>/inventory.json` + a table for the designer.
2. **Export (Unreal)** — skeletal mesh → FBX (skeleton, skin weights, morphs), textures → PNG/EXR (linear for normals /
   ORM), the slot → material → texture map.
3. **Clean-up (Blender)** — import; delete hair (and, optionally, body triangles fully hidden under clothing — checked
   against a few extreme animation poses); keep the armature, weights, morphs.
4. **Atlas UV (Blender + UVPackmaster 4)** — keep each material's original UV islands (no re-unwrap); copy them into a new
   `UV_Atlas` channel; per-group texel-density weights (face / helmet / chest larger, soles / inner parts smaller);
   rotation allowed; padding sized for the atlas resolution (8–16 px at 4K).
5. **Bake (Blender Cycles)** — the mesh bakes from itself: every original material samples its textures through the
   original UVs, the result is written into the atlas through `UV_Atlas`:
   - base colour (emit pass, sRGB);
   - **normal** baked as a normal pass in the tangent space of `UV_Atlas` (islands may be rotated — a plain copy would be wrong);
   - roughness, metallic, AO baked separately and packed into one ORM texture;
   - opacity mask only if a kept part needs it (otherwise opaque);
   - edge dilation 16 px.
6. **Export back (Blender)** — FBX with one material slot `M_Body`, UE-safe settings.
7. **Import (Unreal)** — as a NEW mesh under `/Game/Characters/<owner>/Optimized/` on the SAME skeleton; textures with
   correct compression (BC1/BC7 colour, BC5 normal, BC7/masks ORM, sRGB off for ORM / normal); one material instance of
   the character master material; LODs regenerated; physics asset reused.
8. **Review** — in-engine stills before / after (same pose, camera, light), close-up of the face; slot count and draw
   calls (`stat scenerendering`) with a group of 20 on screen; animation check (no stretched skin, no poke-through).
9. **Swap** — only after the designer's OK: the Blueprint (or the enemy variant) points to the optimized mesh; the
   original stays for rollback.
10. **Batch** — the same scripts over every enemy (the horde is where the saving matters: 55 × 12 slots → 55 × 1).

## Decisions for the designer

- Atlas size per character: 4K (operatives, close camera moments) / 2K (enemies)?
- Remove body parts hidden under clothing? (fewer triangles, risk of holes in extreme poses)
- Which mesh first: the operative `SK_Post_Apo_Survivor_a` (14 slots) as the pilot.
- Master material for characters: we author it (no longer Gemini's) — simple Opaque, BaseColor / Normal / ORM + tint / frost parameters.

## Option B (later): Texture2DArray instead of an atlas

One slot, but each part keeps its original texture resolution: the parts' UVs are offset into integer tiles
(layer index), the master material samples a `Texture2DArray` by `floor(UV)`. No baking, no quality loss; needs all
layers at one size / format. Worth it for hero characters if the atlas looks too soft.

## Implementation notes (pilot `SK_Post_Apo_Survivor_a`, 2026-10-20)

Scripts (source of truth, re-runnable for any character; paths / names / resolutions are arguments):

| Stage | Script | Run |
|---|---|---|
| 1 inventory + export (Unreal, read-only) | `Scripts/Pipeline/unreal_export_inventory.py` | `Scripts/Pipeline/run_unreal_export.ps1 -Mesh /Game/... -Out ArtPipeline/... [-AnimRegex ...]` |
| 2 clean-up | `Scripts/Pipeline/blender_clean.py` | `Scripts/Pipeline/run_blender_pipeline.ps1 -AssetDir ArtPipeline/... [-Stages clean,reduce,atlas,bake,export,review]` |
| 2b reduce (decimate) | `Scripts/Pipeline/blender_reduce.py` | same wrapper (`-ExtraArgs @{reduce="--target-tris 30000"}` for enemies) |
| 3 atlas uv | `Scripts/Pipeline/blender_atlas.py` | same wrapper |
| 4 bake | `Scripts/Pipeline/blender_bake.py` | same wrapper |
| 5 export | `Scripts/Pipeline/blender_export.py` | same wrapper |
| 6 review | `Scripts/Pipeline/blender_review.py` | same wrapper |
| 7 import | `Scripts/Pipeline/unreal_import_optimized.py` | `Scripts/Pipeline/run_unreal_import.ps1 -AssetDir ArtPipeline/... -Dest /Game/Characters/Operatives/Optimized/<Name>` (no editor may run; Content snapshot before / after via `content_snapshot.py`) |
| 8 in-game check | `Source/CodexTacticsDev/Private/Debug/OptimizedCharacterSmokeCommand.cpp` | `Scripts/smoke_parallel.ps1 -Commands OptimizedCharacterSmoke` (asserts); rendered stills: see the file header |
| shared | `pipeline_common.py` (paths, FBX import in cm, PNG writer), `material_graph.py` (UE graph -> Blender nodes, texture roles) | |

Outputs per asset folder: `01_Source/inventory.json` (+ `texture_roles.json`, `slot_decisions.json`: ground-truth
tables for later automatic triage), `03_Blender/*.blend` + stage logs, `04_Textures_Baked/T_<Name>_{BaseColor,Normal,
ORM,Sheen}.png` (+ previews), `05_Export/<SK>_1slot.fbx` + `export_manifest.json`, `06_Review/` renders +
`summary.json` (every stage merges its numbers; the `qa` block holds the triage numbers).

What we learned:

- **Unreal headless export.** `-run=pythonscript` works while no editor holds the project. Paths with spaces do not
  survive `-script="file args"`: the wrapper passes the arguments as JSON in `CODEX_PIPELINE_ARGS`. The skeletal-mesh
  FBX exporter builds a render proxy and asserts (`MeshObject`) under `-nullrhi`: run with
  `-AllowCommandletRendering -RenderOffscreen` (wrapper default). Material baking on export is disabled. Nothing is saved.
- **Material truth comes from the graph, not from names.** Most of the pack's materials are plain `Material`s with
  hard-wired `TextureSample`s (no parameters), so the inventory walks `get_material_property_input_node` -> inputs
  recursively and records texture + channel + math per output. The bake reproduces exactly what Unreal feeds the
  material: this pack's ORM textures are sRGB ON (decoded with the sRGB curve) and roughness is
  `Power(ORM.G, 0.4..0.7)`; the baked ORM stores the FINAL linear values (the new master material must not apply
  Power / sRGB again). Cloth materials add `Fresnel * 0.07` to BaseColor: view-dependent, so it is kept out of the
  BaseColor bake and stored as a 1K `Sheen` mask (`BaseColor + Fresnel * Sheen` in the master material restores it).
  `M_Carabiners` samples its ORM with a Normal sampler (authoring error: Unreal reconstructs Z / remaps to -1..1);
  the bake uses the intended raw channels, so the carabiners will read as metal (today they render mostly non-metal).
- **This mesh has no hair and no body**: the operative is fully clothed (hood + gas mask, gloves), no skin / hair
  slots exist. The hair regex (`HAIR_REGEX_DEFAULT`) stays for other characters; plain `lash` must not be used (it
  matches `Flashlight`).
- **Hidden triangles** (`blender_clean.py hidden_faces`): per triangle 4 sample points (centroid + 3 inner corners)
  cast rays over the front hemisphere (80-direction Fibonacci sphere, about 40 used); one escaping ray = visible.
  Single-sided like Unreal: a hit on a face turned towards the viewer blocks, back faces are looked through. Tested in
  the rest pose, 9 procedural extreme poses (deep crouch, kneel-aim, arms up, hug, twist / bend both ways, prone
  look-up, lunge, sit-reach; rotation signs resolved from the character's forward = head -> mask) and 2 frames of each
  exported animation of the skeleton (8 anims: cold / fire loops, jump, run). Only triangles hidden in ALL 26 poses
  count; a 1-ring safety margin then keeps every hidden triangle sharing a vertex with a visible one. Pilot: about 29k
  triangles hidden in all poses, 4.4k removed after the safety ring (rope coils inside the bundle, boot shafts under
  the gaiters, mask back under the hood, glove cuffs under the sleeves, straps pressed on the backpack). `--keep-ring 0`
  would remove all 29k; judged too risky before an in-engine animation check. Cost ~25 min (Python BVH rays).
- **Atlas.** Original islands are copied into `UV_Atlas` (UV0 stays the bake source). Islands whose uv boxes overlap
  are clustered (the artist's texel sharing); each cluster is scaled to the wanted texel density (per-material weights:
  mask / face x2.0, hoodie / chest x1.4, ropes / carabiners x0.7, down-facing clusters x0.5; never above the source
  density) and moved into its own tile outside 0..1 before packing. Without that, islands of different materials
  (all authored in the same 0..1) get locked together as "overlapping" and share texels (first pilot run: the mask
  showed hoodie cloth). `cross_material_overlap_percent` in the summary checks it.
- **Mirrored + stacked islands may only rotate by 180 degrees.** A 90-degree turn rotates a mirrored tangent frame
  the other way, so the normal bake of one half would overwrite the other with wrong values; such clusters get
  UVPackmaster's per-island rotation step 180 (`RotStepIParamInfo`).
- **UVPackmaster 4 headless works**: it creates `gpu` shaders at registration, so `gpu.init()` must run before
  `addon_utils.enable("bl_ext.user_default.uvpackmaster4")` under `--background`; the non-interactive `pack` operator
  runs synchronously with the PRO engine. Pilot: 902 islands, 60 s heuristic search, 12 px margin + 4 px border at
  4K, rotation step 90 (45 gave the same coverage), coverage ~78 %. Blender's packer remains as the fallback
  (`--builtin`; no rotation when mirrored stacks exist).
- **Reduce** (`blender_reduce.py`, non-destructive: reads `_clean.blend`, writes `_reduce.blend` with the reduced
  `<SK>` and an untouched `<SK>_high` bake source). Collapse decimation per material with a budget
  `ratio = min(1, w * g)` solved for the target (default 50 000 for operatives). Weights: body / mask / gloves / boots
  keep everything (they are already low-poly and deform), radio / flashlight 1.0, ropes 1.0 (thin coils fall apart
  below ~20 %: first try at 0.35 gave noodles), backpack 0.8, pouches 0.7, carabiners 0.7, axe / shovel 0.6. Before
  decimating, split render vertices are welded (0.001 cm, sharp edges kept). A vertex group steers the collapse away
  from uv seams, material borders, open borders and skin-blend zones (Blender collapses HIGH group weights first, so
  the group holds 1 - protection). Normals of the reduced mesh: smooth + sharp by angle 60 degrees, frozen as custom
  normals so the bake, the per-material split and the FBX all use the same frame.
- **Bake** (Cycles, OptiX on the RTX 3080). With a high mesh present the bake is a projection (selected-to-active,
  cage extrusion 0.6 cm, max ray 3 cm): each material separately (only that material's high part is a ray target,
  so straps do not paint the cloth under them), the low mesh's other faces bake into an 8 px dummy image; per-pass
  margins would overwrite neighbours, so the 16 px dilation is a numpy pass afterwards. Ray misses are measured
  against the low mesh's own coverage (`06_Review/bake_misses.png`, red = miss). Normals that come back pointing into
  the surface (rays reaching the back of thin buckles; first try with a 1.5 cm cage rendered them black) are clamped
  to the front hemisphere. Maps: BaseColor 4K sRGB, AO / Roughness / Metallic 2K each packed into ORM, Sheen 1K, a
  tangent-space NORMAL in the `UV_Atlas` frame written DirectX (green down) as a 16-bit PNG. No Emissive map
  (designer decision 2026-10-20). Without a high mesh the mesh bakes from itself (first pilot run).
- **Export / skeleton.** Importing the Unreal FBX into a scene with unit scale 0.01 keeps everything in centimetres
  at object scale 1 (no x100 fix-ups). The importer turns the FBX `root` bone into the armature OBJECT; the exporter
  adds a real `root` bone back (identity, at the origin) and names the object `Armature` (Unreal drops that node), so
  the hierarchy is root -> pelvis / ik_foot_root / ik_hand_root as in `UE4_Mannequin_Skeleton`. A Blender round trip
  verifies bone names, parents and rest positions (max deviation 0.0001 cm), one slot, one uv map, no leaf bones.
- **Review** (Cycles, neutral light, same cameras, original = untouched mesh with all 14 materials) plus a pose sheet
  (`06_Review/pose_sheet.png`: crouch, kneel-aim, arms up, twist, prone look-up; top row original, bottom row reduced)
  for joint collapse / interpenetration. Pilot result (50k tris): mean CIE76 dE 1.07-1.43 per view (p95 3.8-5.9),
  silhouette IoU >= 0.996, ray misses 0.04 %, 1.9 % of normal texels clamped; estimated texture memory 437 MB
  (44 source textures, full mips) -> 35 MB. Materials the reduce stage leaves untouched (mask, hoodie, pants,
  boots / gloves) bake from themselves inside the projection run and keep their authored normals (projection onto
  identical geometry only added ray errors at the hood rim). Known soft spot: the backpack shoulder straps (reduced
  to 17 %) show lighter patches in the face close-up.

Stage 7+ (Unreal import) needs: import `05_Export/<SK>_1slot.fbx` onto `UE4_Mannequin_Skeleton` under
`/Game/Characters/<owner>/Optimized/` (Import Normals, MikkTSpace tangents; check the facing direction and bone
positions against the original), textures BaseColor sRGB BC1, Normal BC5 (DirectX, no Flip Green), ORM linear
TC_Masks (final values), Sheen BC4, no Emissive; a character master material (Opaque:
BaseColor + Fresnel * Sheen, ORM unpacked without Power, Normal); reuse `SK_Post_Apo_Survivor_a_PhysicsAsset`;
regenerate LODs; then the in-game smoke (slot count, draw calls, animation poke-through especially at the removed
boot shafts / glove cuffs / mask back).

### Stage 7-8 (pilot, 2026-10-20)

- Import (`run_unreal_import.ps1`): refuses to run while any UnrealEditor process is up and over existing assets (unless
  `-AllowExisting`), snapshots every .uasset / .umap outside the new folders before and after (path + size + mtime, sha1 of
  the git-tracked ones): 0 added / removed / changed. New assets only: `/Game/Characters/Operatives/Optimized/Post_Apo_Survivor_a/`
  (SK_..._1slot, MI_..., T_..._BaseColor / Normal / ORM / Sheen) and `/Game/Characters/Materials/M_CharacterMaster`.
  Lessons: UE 5.8 imports the FBX through Interchange even with `FbxImportUI` options (warning "invalid bind poses" for the
  added `root` bone, harmless); texture parameters of a new master need a default texture of the matching sampler type or
  the material does not compile; LODs are generated in Unreal (`regenerate_lod`, 3 LODs: 49 994 / 12 497 / 6 248 tris).
- In-game (`CodexTactics.OptimizedCharacterSmoke`, own arena, fixed 60 fps): the leader keeps the original body, the second
  operative gets the optimized one at runtime (same anim class / mesh transform, LOD0 forced for the stills); idle, walk,
  crouch, prone, aim, vault (each body airborne), low cover. Asserts: 1 slot (MI of M_CharacterMaster, 4 textures),
  LOD0 ~50k, >= 3 LODs, same skeleton + physics asset, bounds within 10 %, no warnings about the new assets, hands away
  from the reference pose in every state (no T-pose). Texture memory from the platform data (rendered run): original
  437.3 MB in 44 textures -> optimized 36.0 MB in 4. Under -nullrhi the platform data is not loaded (logged as 0).

## Roll-out: every body the game uses (inventory 2026-10-20)

Found by `Scripts/Pipeline/unreal_character_roster.py` (read-only: BP_CodexTacticsGameMode's operative Blueprint + Squad
Roster, every BP_Operative* / BP_Enemy_* under /Game/Characters, the enemies' LookVariants) -> `ArtPipeline/Characters/roster.json`;
triangle counts from the exported FBX (`roster_counts.json`). The roster of BP_CodexTacticsGameMode has no member Blueprint /
body override today: all three squad members wear BP_Operative (SK_Post_Apo_Survivor_a); BP_Operative_FemaleSoldier is the
alternative operative Blueprint.

Budgets (user 2026-10-20): operatives ~50k triangles, 4K BaseColor / Normal, ORM 2K; enemies ~20k (below the operatives), 2K
BaseColor / Normal, ORM 1K; dogs ~10k; big enemies (Brute) ~25k. No Emissive unless something really glows. Texture-set
variants of one body: one reduced mesh + one atlas, one texture set + MI per variant.

| Order | Asset (ArtPipeline / Optimized folder) | Body mesh | Used by | Tris | Slots | Target | Notes |
|---|---|---|---|---|---|---|---|
| done | Operatives/Post_Apo_Survivor_a | /Game/Post_Apo_Survivor/Models/SK_Post_Apo_Survivor_a | BP_Operative (all 3 squad members) | 213 548 | 14 | 50k | pilot, imported |
| 1 | Operatives/Female_Soldier | /Game/Female_Soldier/Mesh/SK_Female_soldier | BP_Operative_FemaleSoldier | 150 747 | 33 (21 materials) | 50k | hair / lashes (masked) removed; translucent glass + eye layers removed; glow sticks / beacon -> Emissive if it really glows |
| 2 | Enemies/FrozenPMC_Fighter | /Game/Characters/Enemies/PMC/FrozenPMC_Fighter/Mesh/SKM_FrozenPMC_Fighter | BP_Enemy_FrozenPMC_Fighter | 33 613 | 6 | 20k | |
| 3 | Enemies/MilitaryZombie_1 | /Game/military_zombie_set2/Meshes/base_mesh/SKM_base_mesh_set2_1 | BP_Enemy_MilitaryZombie (LookVariants body 1) | 69 770 | 5 | 20k | 3 texture sets (Set1-3) |
| 4 | Enemies/MilitaryZombie_2 | .../SKM_base_mesh_set2_2 | BP_Enemy_MilitaryZombie (body 2) | 51 559 | 5 | 20k | 3 texture sets; hair slot removed |
| 5 | Enemies/MilitaryZombie_3 | .../SKM_base_mesh_set2_3 | BP_Enemy_MilitaryZombie (body 3) | 19 591 | 2 | 20k | 3 texture sets |
| 6 | Enemies/Brute | /Game/Mutant_monster_1/Meshes/base_mesh/SK_base_mesh_1 | BP_Enemy_Brute | 25 186 | 2 | 25k | bare body: only truly hidden parts go |
| 7 | Enemies/Marksman | /Game/Biochemical_Monster_1/baze_mech/ue4/SKM_baze_mesh | BP_Enemy_Marksman | 17 992 | 2 | 20k | already under budget: 1 slot + 2K maps |
| 8 | Enemies/Cutter | /Game/Biochemical_Monster_2/baze_mech/ue4/SKM_baze_mesh | BP_Enemy_Cutter | 28 592 | 2 | 20k | |
| 9 | Enemies/Hound | /Game/Combat_Dog/Mesh/SK_Combat_Dog | BP_Enemy_Hound | 13 904 | 1 | 10k | already 1 slot: reduction + 2K maps |
| 10 | Enemies/Frostbitten | /Game/mutant_monster_2/base_mesh/SK_base_mesh | BP_Enemy_Frostbitten | 24 142 | 1 | 20k | already 1 slot: reduction + 2K maps |

Per-asset settings live in `ArtPipeline/Characters/<Group>/<Asset>/pipeline.json` (target, keep / texel weights by material
name). No Blueprint is switched to the optimized meshes (the user's call); `CodexTactics.OptimizedCharacterSmoke` swaps them at
runtime, one case per asset.

### Look variants (texture / colour sets, user 2026-10-20)

"Bring every colour variant the packs ship": `Scripts/Pipeline/unreal_variant_scan.py` (read-only) finds, for every slot
material of a body, the pack materials whose path differs only in its numbers (set_1 / set_2, _1st / _2nd, MA_x_1st /
M_x_2nd_Inst, M_Outfit_Hoodie / M_Outfit_Hoodie_Inst_2nd); materials with the same numbers form one variant, a slot without
a variant material keeps the mesh's own. Result: `ArtPipeline/Characters/variants.json` and per asset
`material_sets.json` (fed to the inventory export). One reduced mesh + one atlas per body; one texture set +
`MI_<Asset>_<Variant>` per variant; the mesh's slot gets its own look's MI. Already imported bodies get extra variants with
`blender_variant_bake.py` (bake target = the exported low mesh, so the imported mesh stays unchanged) and
`run_unreal_import.ps1 -VariantsOnly Set2,Set3` (textures + MIs only).

| Body | Variants (the first = the look the mesh carries today) |
|---|---|
| Post_Apo_Survivor_a | Set1 (imported as MI_Post_Apo_Survivor_a), Set2 / Set3 = M_Outfit_Hoodie/Pants_Inst_2nd / _3rd |
| FrozenPMC_Fighter | Default (jacket 4th, arms 2nd, pants 2nd, boot / glove / helmet 1st; imported as MI_FrozenPMC_Fighter), Set1..Set5 = the pack's 1st..5th materials per part (a part without that number keeps its own) |
| MilitaryZombie_1 / _2 / _3 | Set1, Set2, Set3 (the BP's LookVariants material sets; helmet / hair only exist in set 1) |
| Brute | Set2 (own), Set1, Set3, Set4 |
| Marksman | Set1 (own), Set2, Set3, Set4, Set5 |
| Cutter | Set3 (own), Set1, Set2, Set4 |
| Hound | Set1 (own), Set2, Set3, Set4, Set5 (M_Combat_Dog_skin_1..5) |
| Frostbitten | Set4 (own), Set1, Set2, Set3 |

Female_Soldier (operative): the pack has no material sets; its camouflage is a texture parameter of its materials (not
enumerated).

Traps found in the roll-out (fixed 2026-10-20):
- Equally named textures in different folders (`military_zombie_set2/texture/set_1/T_bodi_set_2_BaseColor` and
  `.../set_2/T_bodi_set_2_BaseColor`; also Brute, Cutter, Marksman, Frostbitten): the export wrote them to ONE png, so every
  variant baked the same look. The inventory now maps each texture to its own png stem (`texture_files`:
  `<name>__<folder>` for clashing names, `unreal_export_inventory.texture_file_stems`); the Blender side resolves through it
  (`material_graph.texture_stem`). Inventories without the map keep the old naming.
- `blender_variant_bake.py`: the low mesh only carries `UV_Atlas`, so the variant materials must sample the high's source uv
  (`apply_variant` takes the uv from the high); the inner Blender runs with `--python-exit-code` so a failing bake fails the
  wrapper.
- The smoke dresses the ORIGINAL body in the variant's source materials from `material_sets.json` (by slot name), so both
  bodies in a still show the same texture set.

Hair rule (user 2026-10-20): hair is removed ONLY where it is not visible (under helmets / hoods / masks). Enemies keep their
hair (`pipeline.json` `"hair_regex": ""`; hair under a helmet goes with the hidden-in-all-poses test); operatives use the
default regex. Brows / lashes that would be lost with the hair (Female_Soldier `M_Female_Soldier_Eyes_Hair`) are painted
into the skin: `pipeline.json` `"bake_overlay_regex"` imports those faces from the source FBX as a bake-only overlay
(`blender_bake.build_overlay` / `bake_overlay`: their colour over the baked BaseColor by their opacity; no geometry in the
low). The translator supports the engine functions CheapContrast / Contrast_Preserve_Color (the latter approximated).
Emissive stays at the 0.1 % coverage rule (the Female_Soldier helmet beacon, 0.016 %, gets no glow).
