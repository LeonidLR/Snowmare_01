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
