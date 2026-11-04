# Report: T5. PMC Jacket Material - Tint and Snow Parameters

**Task:** T5. PMC jacket material - tint and snow parameters  
**Branch:** `gemini/t5-pmc-materials`  
**Date:** 2026-11-04  

## Summary of Deliverable

Implemented master material `M_PmcOperator_Body` under `/Game/Characters/Enemies/PMC/Operator/Materials/` and re-parented the three PMC Operator material instances (`MI_PmcOperator_Grey`, `MI_PmcOperator_Dark`, `MI_PmcOperator_Snowed`) to give them distinct visual variants (grey, dark, snowed) with tinting, world-up snow accumulation, and grime parameters, without modifying the user's untracked pack (`Content/Cyberpunk_Explorer_A`).

Automated setup via headless Editor Python script [`Scripts/Editor/setup_pmc_body_material.py`](../../Scripts/Editor/setup_pmc_body_material.py). Verified with automated smoke suites and captured a `-RenderOffscreen` screenshot of the three variants side by side.

---

## 1. Master Material Architecture (`M_PmcOperator_Body`)

The master material was created in `/Game/Characters/Enemies/PMC/Operator/Materials/M_PmcOperator_Body` with standard PBR shading and custom parameter expressions:

- **Texture Parameter References** (Referencing Cyberpunk pack textures without copying files or editing pack assets):
  - `basecolor` (`Texture2D'/Game/Cyberpunk_Explorer_A/Textures/T_Jacket_Body_A_BaseColor.T_Jacket_Body_A_BaseColor'`)
  - `ORM` (`Texture2D'/Game/Cyberpunk_Explorer_A/Textures/T_Jacket_Body_A_OcclusionRoughnessMetallic.T_Jacket_Body_A_OcclusionRoughnessMetallic'`, SamplerType Color)
  - `Normal` (`Texture2D'/Game/Cyberpunk_Explorer_A/Textures/T_Jacket_Body_A_Normal.T_Jacket_Body_A_Normal'`)
  - `Mask` (`Texture2D'/Game/Cyberpunk_Explorer_A/Textures/T_Jacket_Body_A_Mask.T_Jacket_Body_A_Mask'`)
- **Parameters**:
  - `Tint` (`VectorParameter`): Multiplies base color on the cloth via jacket mask / luminance isolation.
  - `BaseColorFactor` (`VectorParameter`): Compatible role-color tint hook for runtime character coloring.
  - `SnowAmount` (`ScalarParameter`, 0.0 .. 1.0): Controls world-up accumulation of snow. Calculated via dot product of Pixel World Normal with World Up `(0, 0, 1)`, blended with a high-roughness white snow layer (`Roughness = 0.9`).
  - `Grime` (`ScalarParameter`): Darkens crevices and adjusts micro-roughness for weathered combat wear.
- **Shader Compilation**: 0 errors across all PCD3D_SM6 shaders.

---

## 2. Configured Material Instances

The three existing material instances under `/Game/Characters/Enemies/PMC/Operator/` were re-parented to `M_PmcOperator_Body` with visibly distinct settings:

1. **`MI_PmcOperator_Grey`**:
   - `Tint` / `BaseColorFactor`: `(0.604, 0.620, 0.639, 1.0)` (Mid tactical grey)
   - `SnowAmount`: `0.0`
   - `Grime`: `0.15`
2. **`MI_PmcOperator_Dark`**:
   - `Tint` / `BaseColorFactor`: `(0.200, 0.216, 0.231, 1.0)` (Stealth charcoal / black)
   - `SnowAmount`: `0.0`
   - `Grime`: `0.05`
3. **`MI_PmcOperator_Snowed`**:
   - `Tint` / `BaseColorFactor`: `(0.847, 0.863, 0.878, 1.0)` (Winter alpine camo)
   - `SnowAmount`: `0.85` (Heavy snow accumulation on shoulders, collar, and upward surfaces)
   - `Grime`: `0.20`

---

## 3. Automation Scripts

- [`Scripts/Editor/setup_pmc_body_material.py`](../../Scripts/Editor/setup_pmc_body_material.py): Headless script executed via `UnrealEditor-Cmd.exe CodexTactics.uproject -run=pythonscript -script=...`. Automatically sets up material expressions, links texture parameters, compiles shaders, re-parents the instances, and overrides parameters.
- **Offscreen Capture Workflow (Note)**: The side-by-side shot was generated headless via offscreen Python commandlet using Slate viewport capture with high-resolution screenshot request in offscreen mode. To keep the branch strictly compliant with Section 3 allowed paths (`Scripts/Editor/setup_pmc_body_material.py`), the capture helper script is retained locally/in reports rather than committed.

---

## 4. Verification Evidence

### Automated Smoke Suites
1. **`CodexTactics.PmcTeamSmoke`**:
   - Map: `/Game/Maps/L_MovementTest`
   - Result: **Smoke RESULT: PASS**
   - Asserts passed: PMC operative spawning, team 1 registry, body mesh `SKM_PmcOperator`, `ABP_Operative` animation driving, team sight visibility, combat targeting, wall occlusion, and monster self-defence targeting.
2. **`CodexTactics.PmcArenaSmoke`**:
   - Map: `/Game/Maps/L_FormationLabCombat` (`-LevelJson=L_PmcArena.json`)
   - Result: **Smoke RESULT: PASS**
   - Asserts passed: Full 3v3 PMC squad combat simulation using all three looks (`grey`, `dark`, `snowed`) across both teams, track generation, health loss, and round completion.

### Offscreen Screenshot
- Output path: `Saved/Screenshots/PmcMaterials/pmc_variants_side_by_side.png`
- File size: 456,828 bytes
- Resolution: Offscreen 16:9 render
- Contents: Three `SKM_PmcOperator` characters side-by-side (Grey on left, Dark in center, Snowed on right) showcasing distinct cloth tint, snow accumulation, and grime under level lighting.
