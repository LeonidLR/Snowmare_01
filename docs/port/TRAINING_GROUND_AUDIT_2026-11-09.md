# Training Ground vs game: per-level settings audit (2026-11-09)

Read-only audit requested by the game designer ("I want to control every level through the Training Ground: weather, each
operative's weapons on each level, everything"). Evidence is file:line in `Source/CodexTactics/` and `Tools/TrainingGround/`.
Level parser: `Private/Data/LevelJsonRules.cpp` (`ParseLevel` 352-625); level JSON loader `Private/Core/CodexTacticsGameMode.cpp:192-215`
(`-LevelJson=` > `<MapName>.json` > `level_01_outpost.json`). Campaign file: `Content/Data/Campaign/campaign.json` (TG reads it only to group
the level list).

## Key findings
1. Weather is readable per level by the game (`environment{...}`, `LevelJsonRules.cpp:123-200,503-511` -> `EnvironmentSubsystem.cpp:209-257`)
   but TG has no panel. Per-operative weapons have no per-level key at all (global `weapons_tuning.json` `loadouts` by role,
   `CodexTacticsGameMode.cpp:339-357`).
2. Dead / ignored TG fields in normal play: most `squad_loadout` numbers (turrets, barricades, mines, medkits, M16 / pistol ammo apply only in
   EDITOR_PRESET, `LoadoutRules.cpp:4-71`; skipped on a campaign arrival, `MissionSubsystem.cpp:325-330`), canned food / matches (not parsed),
   loot / secret toggles (telemetry only, `RunTelemetryRules.cpp:116-117`), prep / rest timers unless a wave is "prepared", the whole waves tab
   on skirmish levels (`WaveSubsystem.cpp:160-172`; being fixed: waves feed the firefight horde), wave `cold_drain_mult` on skirmish levels,
   `max_simultaneous_enemies` / `spawn_delay_sec` / `initial_delay_sec` (parsed, never used: `WaveSubsystem.cpp:354-366`).
3. Combat Lab (pressure, cheats) lists only L_FormationLabCombat and generated maps with a `bot_route` (`combatLab/maps.ts`).

## Problems
- A. Level weather is ignored on a campaign arrival unless `environment.locked` (`EnvironmentSubsystem.cpp:178-191`); on locked levels weather
  does not scale the cold (`cold_on_locked_levels=false`, `EnvironmentRules.cpp:426-429`).
- B. Suspected bug: `ApplyStageLoadout()` at `MissionSubsystem.cpp:220` sits outside `if (bCutsceneEnded)` (207-219) -> re-applies on every
  game-phase change (grenades reset at each fight start / end). Verify.
- C. Waves-tab save writes the whole stale level object (`App.tsx:400-413` -> `vite.config.ts:164`) and can revert Combat Lab / hand edits.
- D. "Run simulator" writes `level_01_outpost.json` + `<level_id>.json` (`vite.config.ts:563-569`) and always plays L_MovementTest
  (`Scripts/bot_run.ps1:29`).
- E. The stealth tab addresses files by `level_id`; `level_01_outpost.json` has `level_id: stage_01`, so its edits land in `stage_01.json`.
- `types.ts:174` comment still says horde default 6 (code: 10); grenades field shows 2 when the key is absent (game reads -1 = keep).

## Game-read per-level keys not in TG
`environment.*` (top priority), `corpse_cap`, `ambush_fight`, `susanin_wave`, `gate_exit`, `start_fade_seconds`, top-level `intro_dialogue`,
`pmc_squads[]`, `mortar_nests[]`, `spawns[].environment_lock`, `pressure` / `cheats` on non-lab levels (+ technical: `drop_links`,
`minimap`, `vehicle.shells`, `bot_route`).

## Global today, wanted per level
Weapons per operative (`weapons_tuning.json` loadouts), starting kit / ammo, squad composition (GameMode BP `SquadRoster`), solo / formation
start (no data), enemy weapons / stats, PMC weapon pool (`pmc_operators.json`), weather dynamics (`environment.json`), ROE / suppression /
under-fire tuning, camera / formation cvars (a level `cvars` block would cover many).

## Recommended order (top 15)
1. Environment panel (weather, time, lock, climate, snow / wetness) + explain the lock / carry / cold rules.
2. Weather carry fix: `environment.override_carry` (or per-level `cold_on_locked_levels`).
3. Per-level `loadouts {role or slot: [weapon ids]}` read before `WeaponLoadoutRules::Find` + TG weapon picker.
4. Per-operative starting kit (grenades, medkits, reserve ammo, heat packs) applied in normal play; campaign-carry policy.
5. Fix bug B first. 6. Fix save hazard C (patch changed keys). 7. Fix simulator D (`-Map` + `-LevelJson`, stop writing outpost JSON).
8. Show the level type in the waves tab and grey out dead panels. 9. Expose `ambush_fight`, `susanin_wave`.
10. PMC squads editor (+ later per-squad weapon pool / stats). 11. Combat Lab pressure / cheats on every level.
12. Enemy mix per level for non-wave levels; `environment_lock` checkbox; CRYO_DRONE + several entries per type in the wave editor.
13. `corpse_cap`, `start_fade_seconds`, `intro_dialogue`, `gate_exit`. 14. Level `cvars` block. 15. Squad composition + start mode per level.
