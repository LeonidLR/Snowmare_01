# Gemini task: Training Ground per-level control, pass 1 (2026-11-09)

You are Gemini working on CodexTactics (Unreal Engine 5.8.2 C++ tactical game + a Vite/TypeScript "Training Ground" web tool).
Claude is the orchestrator and the only one who merges into `main`. The game designer approved this task.

## Where to work (strict)
- Worktree: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-gemtg`, branch `gemini/tg-per-level` (already created from main).
- Work ONLY in that folder. Never edit, build or test in `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics` (the main
  checkout: the designer's editor runs there and his uncommitted edits of `Content/Data/LevelJson/L_Gen_Depot_1.json` live there).
- Set `$env:CODEX_AGENT = "gemini"` in every shell. Commit on your branch with explicit paths (never `git add -A`), Conventional Commits.
  Do NOT push, do NOT merge, do NOT touch `main`. Do not modify `Content/Maps/**`, `Config/DefaultEditor.ini`, `Config/DefaultScalability.ini`,
  any `.uasset`, or files of the untracked asset packs under `Content/` (they are junctions to the main checkout - deleting through them
  destroys the designer's assets).
- Do not change `Content/Data/LevelJson/L_Gen_Depot_1.json` (the designer's file). For tests use a copy under a test name.
- Another Claude agent is editing the Training Ground horde / waves UI in parallel (branch `feat/horde-300m-waves`: a "waves feed the firefight
  horde" note and a 300 m horde distance field). Keep your TG changes out of the horde / waves panels except bug fix C below, so the merge
  stays clean.

## Read first
`CLAUDE.md`, `docs/port/HANDOFF.md`, `docs/port/TRAINING_GROUND_AUDIT_2026-11-09.md` (the audit this task comes from - file:line evidence),
`Tools/TrainingGround/vite.config.ts`, `Tools/TrainingGround/src/App.tsx`, `src/types.ts`, the level parser
`Source/CodexTactics/Private/Data/LevelJsonRules.cpp` (`ParseLevel` 352-625) and `Public/Data/WaveConfigTypes.h`.

## What to build
1. **Environment panel (per level)** in the Training Ground: edit the level JSON `environment{locked, start_hour, weather, climate, seed,
   visuals, snow_cover, wetness, puddles, exposure_bias}` (the game already reads them: `LevelJsonRules.cpp:123-200,503-511` ->
   `EnvironmentSubsystem.cpp:209-257`; types exist in `types.ts:137-154,197`). Weather / climate values = the ones the C++ accepts (read the
   parser / `environment.json`). Show in the panel, in Russian UI text like the rest of the TG: "locked = weather and clock frozen for the
   whole level; on a locked level weather does not change the cold".
   **Game fix:** on a campaign arrival the level's start weather / hour is ignored unless `locked` (`EnvironmentSubsystem.cpp:178-191`).
   Add `environment.override_carry` (bool, default false): when true the level's `weather` / `start_hour` win over the carried state even if not
   locked. Parse it in `LevelJsonRules`, apply in `EnvironmentSubsystem`, expose it in the panel.
2. **Weapons of every operative on every level:** new level JSON key `loadouts {"commander": [...], "engineer": [...], "medic_sapper": [...],
   "recruit": [...]}` (weapon ids from `Content/Data/Weapons/weapons_tuning.json` `weapons`: m16, pistol, shotgun, sniper_rifle, rpg7, grenade).
   The game reads it at spawn BEFORE the global `WeaponLoadoutRules::Find` (`CodexTacticsGameMode.cpp:339-357`); a role missing in the level
   falls back to the global `loadouts`. It must also work on a campaign arrival (the arsenal is rebuilt at spawn; the carry restores ammo and the
   held weapon - `CampaignCarry.cpp:105-116`; if the held weapon is not in the new loadout, take the first). TG: a picker per role with the
   roster's weapons, ordered (the first = in hands), "use global" toggle.
3. **Starting kit per operative that really works in normal play:** grenades, medkits, reserve ammo per weapon, heat packs - per role in the level
   JSON (e.g. `kit {"commander": {"grenades": 2, "medkits": 1, "heat_packs": 2, "ammo": {"m16": 90}}}`), applied in every mode (not only
   EDITOR_PRESET: `LoadoutRules.cpp:4-71`). Campaign policy: on a campaign arrival the carried supplies win unless the level sets
   `kit_override_carry: true` (`MissionSubsystem.cpp:325-330`). Keep the old `squad_loadout` fields working as before (bot / EDITOR_PRESET);
   in the TG label the old fields "bot / simulator only" and hide canned food / matches (not read by the game) or mark them.
4. **Bug fixes found by the audit:**
   - B: verify and fix - `ApplyStageLoadout()` at `MissionSubsystem.cpp:220` sits outside `if (bCutsceneEnded)` (207-219) and seems to re-apply
     on every game-phase change (grenades reset at every fight start / end). It must apply once per level start. Unit test.
   - C: the waves tab save writes the whole stale level object (`App.tsx:400-413` -> `vite.config.ts:164`) and reverts edits saved meanwhile
     by Combat Lab / by hand. Make `/api/save-stage` patch only the changed top-level keys into the file text (reuse the existing
     `patchJsonText` helper the other endpoints use), keep formatting / BOM.
   - D: "Run simulator" writes `level_01_outpost.json` + `<level_id>.json` (`vite.config.ts:563-569`) and always plays `/Game/Maps/L_MovementTest`
     (`Scripts/bot_run.ps1:29`). Make it run the SELECTED level's map with `-LevelJson=<its file>` and stop overwriting `level_01_outpost.json`.
   - The grenades field shows 2 when the key is absent although the game reads -1 = keep (`SquadLoadoutControls.tsx:21,28,294`); `types.ts:174`
     says horde default 6 - the code default is 10.

## Tests and checks (all must pass before you report)
- Build: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (in the worktree).
- C++ unit tests: add tests `CodexTactics.Data.LevelJson.Loadouts*`, `...Kit*`, `...EnvironmentOverrideCarry`, `CodexTactics.Mission.StageLoadoutOnce`
  in `Source/CodexTacticsTests/`; run `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1` (full) and `Scripts/test.ps1 -CheckMap`.
- A smoke (headless console command in `Source/CodexTacticsDev/Private/Debug/`, pattern of the existing `*SmokeCommand.cpp`, registered in
  `Scripts/test_map.json`): a test level JSON with per-level loadouts + kit + environment (blizzard at 22:00, override_carry) -> the Commander
  spawns with the level's weapons in order and kit, the environment starts in that state. Run it with
  `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands <YourSmoke>,WeatherSmoke,EnvironmentSmoke,WeaponSelectorSmoke,DepotCampaignStartSmoke,TutorialSaveSmoke -ExtraArgs "-Multiprocess"`.
- Training Ground: in `Tools/TrainingGround`: `npm test`, `npm run typecheck`, the golden check (`npm run golden` or as in `package.json`).
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent gemini`.

## Docs
Update `docs/systems/waves-horde-encounters.md` only if you touch it; add a section "Per-level loadouts, kit and environment" to the level JSON
schema doc (find it under `docs/` / `Content/Data/LevelJson/level_config.schema.json` - add the new keys to the schema too), a row at the top
of `docs/port/CHANGELOG.md` (branch gemini/tg-per-level), and a short entry in `docs/port/HANDOFF.md`.

## Report back (to the designer, who passes it to Claude)
Commits (hashes), what changed per point 1-4, the test / smoke results with numbers, anything not done and why. Claude reviews the diff and merges.
