# Gemini Weekend Sprint Summary (November 2026)

**Target Checkout:** `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemini`  
**Base:** `origin/main` (`b1a9dc91`)  
**Date:** 2026-11-04  
**Author:** Gemini (Advanced Agentic Pair-Programmer)  

---

## Task Matrix

| Task | Branch | Commit | Status | Primary Verification | Deliverables / Artifacts |
|---|---|---|---|---|---|
| **T1: Training Ground Levels** | `gemini/t1-training-ground-levels` | `d89094bf` | **DONE** | Vitest unit tests (250/250 PASS), TypeScript check, Golden file regression suite | Real level catalog parser, categorization (Campaign / Tactical / Dev), horde threshold editor, `L_Gen_Depot_1.json` update |
| **T2: Weather & Cold Combat** | `gemini/t2-weather-cold-combat` | `996b5a18` | **DONE** | Design & architectural review, formula balance checks | [`docs/design/WEATHER_COLD_COMBAT.md`](../design/WEATHER_COLD_COMBAT.md) |
| **T3: Doorway Breach** | `gemini/t3-doorway-breach` | `0dc430df` | **DONE** | Architectural review, schema & state-machine validation | [`docs/design/DOORWAY_BREACH.md`](../design/DOORWAY_BREACH.md) |
| **T4: Narrative Bible** | `gemini/t4-narrative-bible` | `c3b5a3e6` | **DONE** | Narrative tone review (Gorky 17 tribute), timeline consistency | [`docs/design/NARRATIVE_BIBLE.md`](../design/NARRATIVE_BIBLE.md) |
| **T5: PMC Materials** | `gemini/t5-pmc-materials` | `3cbbf31b` | **DONE** | `CodexTactics.PmcTeamSmoke` (PASS), `CodexTactics.PmcArenaSmoke` (PASS), Offscreen Render | `M_PmcOperator_Body`, 3 re-parented MIs, [`setup_pmc_body_material.py`](../../Scripts/Editor/setup_pmc_body_material.py), side-by-side screenshot |
| **T6: Formation Box Selection** | `gemini/t6-formation-box-selection` | `b0d5df0e` | **DONE** | `Squad.Orders.*` unit tests (10/10 PASS), `SquadControlSmoke` (PASS), `SoloRejoinSmoke` (PASS), `CombatFormationSmoke` (PASS), `GroupCornerPeekSmoke` (PASS), `WallLeadershipSmoke` (PASS) | C++ selection & formation fix in Core & Characters, pure rules, extended smoke assertions |
| **T7: Sight Range / Cone Work** | — | — | **CANCELLED** | N/A | Cancelled 2026-11-04 per plan; sight components/data untouched |

---

## Task Details & Verification Summaries

### T1. Training Ground: Real Level Roster & Horde Threshold
- **Branch:** `gemini/t1-training-ground-levels` (`d89094bf`)
- **Key Changes:**
  - Upgraded [`Tools/TrainingGround/`](../../Tools/TrainingGround/) to discover and display real game levels (`.umap` and `.json`), categorizing them into *Campaign*, *Tactical Arenas*, and *Dev & Test Grounds*.
  - Added dedicated *Level Configuration* editor panel allowing inspection and modification of `horde_threshold_percent` (saved back to level JSON).
  - Updated `Content/Data/LevelJson/L_Gen_Depot_1.json` with initial horde threshold.
- **Verification:**
  - Vitest test suite: `npm test` -> 250 tests passed across 18 test suites (0 failed).
  - Type checking: `npm run typecheck` -> clean, 0 TypeScript diagnostics.
  - Golden regression suite: `npm run golden` -> all snapshot tests matched.
- **Report:** [`docs/port/gemini_reports/t1-training-ground-levels.md`](t1-training-ground-levels.md)

### T2. Weather & Cold Combat: Blizzard vs Sight and Accuracy
- **Branch:** `gemini/t2-weather-cold-combat` (`996b5a18`)
- **Key Changes:**
  - Complete game design and systems architecture document in [`docs/design/WEATHER_COLD_COMBAT.md`](../design/WEATHER_COLD_COMBAT.md).
  - Covers weather severity states (Clear, Light Snow, Heavy Snow, Blizzard), sight range decay models, visibility cone adjustments, wind drift penalties on projectile ballistics, thermals/heat-source interaction, and frostbite stat debuffs.
- **Open Questions Left for Director / Lead Designer:**
  1. *Thermal Scope Availability:* Should thermal scopes completely bypass Blizzard visibility penalties, or degrade to a fuzzy proximity heat signature?
  2. *Wind Drift on Heavy Caliber / Snipers:* Should snipers have specialized ammo (match-grade) that ignores wind drift up to 30 m?
- **Report:** [`docs/port/gemini_reports/t2-weather-cold-combat.md`](t2-weather-cold-combat.md)

### T3. Doorway Breach: CQB Combat & Stack Tactics
- **Branch:** `gemini/t3-doorway-breach` (`0dc430df`)
- **Key Changes:**
  - Architectural CQB doorway breach specification in [`docs/design/DOORWAY_BREACH.md`](../design/DOORWAY_BREACH.md).
  - Formulates squad stack behaviors (Wall Stack, Corner Slice, Pie Slicing), door types (Unlocked, Barricaded, Trapped, Reinforced), breach methods (Kick, Shotgun Slug, C4 Charge, Flashbang / Frag toss), room clearing choreography, and AI reaction cones.
- **Open Questions Left for Director / Technical Designer:**
  1. *Camera Presentation:* Should dynamic doorway breach trigger a brief cinematic camera punch-in or remain strictly tactical isometric?
  2. *Failure Mechanics:* If a kick fails against a reinforced door, does the operator stumble and take a temporary stagger debuff?
- **Report:** [`docs/port/gemini_reports/t3-doorway-breach.md`](t3-doorway-breach.md)

### T4. Narrative Bible (First Draft)
- **Branch:** `gemini/t4-narrative-bible` (`c3b5a3e6`)
- **Key Changes:**
  - Comprehensive 6-page Narrative Bible in [`docs/design/NARRATIVE_BIBLE.md`](../design/NARRATIVE_BIBLE.md) honoring user's 2026-11-02 vision and Gorky 17 tribute tone.
  - Profiles for Sally (Commander), Roger (Engineer), Babai (Medic-Sapper), and Ronin (Antagonist).
  - Complete 20-year chronological timeline (Diamond valley betrayal, PMC "Ronin" ascension, State ГБР contract).
  - Detailed level-by-level campaign progression across Prologue (P1, P2, P3) and Arctic Present (Lake, Depot, Plant, Diamond Mines).
- **Open Questions Explicitly Highlighted:**
  1. *Prologue Scope / Vertical Slice:* Should Prologue P3 (the betrayal mission) serve as the public demo / vertical slice, or should Lake (`L_Demo01_A_FrozenLake`) remain the first playable slice?
  2. *Instructor at Camp (P1/P2):* Is the training instructor an old veteran comrade who later dies, or a cold state handler?
  3. *Setting of Betrayal Mission:* Africa (Sierra Leone border) vs South America (Guyana/Suriname jungle border)?
- **Report:** [`docs/port/gemini_reports/t4-narrative-bible.md`](t4-narrative-bible.md)

### T5. PMC Jacket Material: Tint and Snow Parameters
- **Branch:** `gemini/t5-pmc-materials` (`3cbbf31b`)
- **Key Changes:**
  - Created master material `/Game/Characters/Enemies/PMC/Operator/Materials/M_PmcOperator_Body` referencing existing Cyberpunk pack textures without editing untracked pack files.
  - Added parameters: `Tint` (cloth vector tint), `BaseColorFactor`, `SnowAmount` (world-up vertex normal accumulation), and `Grime` (weathered crevice darkening).
  - Re-parented and configured material instances:
    - `MI_PmcOperator_Grey`: Tactical grey tint, 0 snow, 0.15 grime.
    - `MI_PmcOperator_Dark`: Stealth black tint, 0 snow, 0.05 grime.
    - `MI_PmcOperator_Snowed`: Alpine winter tint, 0.85 snow accumulation, 0.20 grime.
  - Headless setup automation script: [`Scripts/Editor/setup_pmc_body_material.py`](../../Scripts/Editor/setup_pmc_body_material.py).
- **Verification:**
  - `CodexTactics.PmcTeamSmoke`: PASS.
  - `CodexTactics.PmcArenaSmoke`: PASS (full 3v3 PMC arena simulation).
  - Offscreen render captured: `Saved/Screenshots/PmcMaterials/pmc_variants_side_by_side.png` (456,828 bytes, 888x500 offscreen viewport render).
- **Report:** [`docs/port/gemini_reports/t5-pmc-materials.md`](t5-pmc-materials.md)

### T6. Solo -> Formation Selection Clearing & Formation Box Selection Disable
- **Branch:** `gemini/t6-formation-box-selection` (`b0d5df0e`)
- **Key Changes:**
  - Implemented `SquadOrderRules::IsBoxSelectionAllowed(bSoloMode, bTurnBasedCombat)` disabling box selection in formation mode and turn-based combat.
  - Guarded `ACodexTacticsPlayerController::PlayerTick` and `SelectInBox` so mouse ground drag does not spawn selection boxes in formation mode; ground release early-returns if dragged to prevent unintended clicks.
  - Implemented `USquadSubsystem::ClearGroupSelection()` invoked on `ExitSoloMode` (Solo -> Formation switch via **B** key or leash) to reset `SelectedGroup` to `{leader}`, clearing operative multi-selection flags.
- **Verification:**
  - Unit tests: `CodexTactics.Squad.Orders.*` (10/10 PASS).
  - Extended smoke: `CodexTactics.SquadControlSmoke` with Stage 4 (PASS).
  - Regression smokes: `SoloRejoinSmoke` (PASS), `CombatFormationSmoke` (PASS), `GroupCornerPeekSmoke` (PASS), `WallLeadershipSmoke` (PASS).
- **Report:** [`docs/port/gemini_reports/t6-formation-box-selection.md`](t6-formation-box-selection.md)

---

## Hand-Back & Next Steps

1. All tasks T1 through T6 are committed on their dedicated `gemini/*` branches branched strictly from `origin/main`.
2. Protected files (`*.umap`, `Config/DefaultEditor.ini`, `Config/DefaultScalability.ini`, `pmc_commander.json`, `Scripts/Trainer/`, `Saved/Trainer/`) were completely preserved and left untouched.
3. All branches are ready for Claude's final review and integration merge.


---

## Round 2 (Plan 2): Section A — Scouting for Claude's Next Sessions

**Branch:** `gemini/diag-round2`  
**Date:** 2026-11-07  
**Scope:** In-depth read-only architectural investigation and code analysis for Claude's upcoming combat and movement sessions.

### Deliverables Matrix

| Task | Report | Root Cause Summary | Proposed Fix |
|---|---|---|---|
| **#32 (Peek Sight Range)** | [`diag_32.md`](diag_32.md) | Hardcoded `RangeCm = 1800.f` in `FPeekSightConfig` truncates vision cone to 18 m, blinding operatives to targets in weapon reach (60–120 m). | Scale peek sight distance dynamically with `GetSightRangeCm(Operative)` or weapon reach. |
| **#43 Rest (Under Fire Shift & Auto-Cover)** | [`diag_43_rest.md`](diag_43_rest.md) | `UnderFireRules::ShouldReturnFire` halts return fire when `!bLine`, but followers remain locked to rigid column offsets and never seek cover autonomously. | Plug lateral lane shift into `USquadSubsystem::UpdateSectorFollower` and trigger evasive cover search when taking fire. |
| **#36 (Depot Low Obstacle Vaulting)** | [`diag_36.md`](diag_36.md) | Procedural greybox builder `build_levelgen_map.py` spawns low covers (sandbags/jersey barriers) without `"Vault"` tag, so `MakeVaultable` is never called and `TryVault` returns false. | Append `"Vault"` tag to low cover specs (40–130 cm) in level generator or add dimension fallback in `VaultNavigation::IsVaultable`. |
| **#17 / 16c (Ladders & Ramps Traversal)** | [`diag_17_16c.md`](diag_17_16c.md) | Ladder reversal deferred to rung boundary; opposing ramp walkers lack yield rules; combat entry does not break `SetRampHold`; slope speed unmodulated. | Clear ramp holds immediately on combat entry; add opposite-direction yield rule; scale velocity by slope normal. |
| **PMC Formation (Catalog Integration)** | [`diag_pmc_formation.md`](diag_pmc_formation.md) | `USquadSubsystem` is player-hardcoded singleton, whereas `SquadOrderCatalog` contains only individual orders. | Commander-level coordination in `UPmcSquadSubsystem` calculating formation/wall slots and issuing individual `Move`/`TakeCover` orders. |
