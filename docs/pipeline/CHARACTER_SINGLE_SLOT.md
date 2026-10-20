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
| 2 clean-up | `Scripts/Pipeline/blender_clean.py` | `Scripts/Pipeline/run_blender_pipeline.ps1 -AssetDir ArtPipeline/... [-Stages clean,atlas,bake,export,review]` |
| 3 atlas uv | `Scripts/Pipeline/blender_atlas.py` | same wrapper |
| 4 bake | `Scripts/Pipeline/blender_bake.py` | same wrapper |
| 5 export | `Scripts/Pipeline/blender_export.py` | same wrapper |
| 6 review | `Scripts/Pipeline/blender_review.py` | same wrapper |
| shared | `pipeline_common.py` (paths, FBX import in cm, PNG writer), `material_graph.py` (UE graph -> Blender nodes, texture roles) | |

Outputs per asset folder: `01_Source/inventory.json` (+ `texture_roles.json`, `slot_decisions.json`: ground-truth
tables for later automatic triage), `03_Blender/*.blend` + stage logs, `04_Textures_Baked/T_<Name>_{BaseColor,Normal,
ORM,Emissive,Sheen}.png` (+ previews), `05_Export/<SK>_1slot.fbx` + `export_manifest.json`, `06_Review/` renders +
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
- **Bake** (Cycles, OptiX on the RTX 3080, ~45 s for all maps): emit passes for BaseColor (4K sRGB), AO / Roughness /
  Metallic (2K each, packed into ORM), Emissive (2K; only ~0.008 % texels lit - candidate to drop), Sheen (1K); a real
  tangent-space NORMAL bake in the `UV_Atlas` frame written DirectX (green down) as a 16-bit PNG; 16 px dilation
  (adjacent faces). Source normal maps are DirectX and are flipped for Blender when sampled.
- **Export / skeleton.** Importing the Unreal FBX into a scene with unit scale 0.01 keeps everything in centimetres
  at object scale 1 (no x100 fix-ups). The importer turns the FBX `root` bone into the armature OBJECT; the exporter
  adds a real `root` bone back (identity, at the origin) and names the object `Armature` (Unreal drops that node), so
  the hierarchy is root -> pelvis / ik_foot_root / ik_hand_root as in `UE4_Mannequin_Skeleton`. A Blender round trip
  verifies bone names, parents and rest positions (max deviation 0.0001 cm), one slot, one uv map, no leaf bones.
- **Review** (Cycles, neutral light, same cameras, original = untouched mesh with all 14 materials): mean CIE76 dE
  ~1 per view, silhouette IoU 1.0; estimated texture memory 437 MB (44 source textures, full mips) -> 38 MB.

Stage 7+ (Unreal import) needs: import `05_Export/<SK>_1slot.fbx` onto `UE4_Mannequin_Skeleton` under
`/Game/Characters/<owner>/Optimized/` (Import Normals, MikkTSpace tangents; check the facing direction and bone
positions against the original), textures BaseColor sRGB BC1, Normal BC5 (DirectX, no Flip Green), ORM linear
TC_Masks (final values), Sheen BC4 (optional), Emissive (optional); a character master material (Opaque:
BaseColor + Fresnel * Sheen, ORM unpacked without Power, Normal); reuse `SK_Post_Apo_Survivor_a_PhysicsAsset`;
regenerate LODs; then the in-game smoke (slot count, draw calls, animation poke-through especially at the removed
boot shafts / glove cuffs / mask back).
