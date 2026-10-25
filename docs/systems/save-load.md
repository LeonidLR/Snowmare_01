# Save / load

> `USaveGameSubsystem` writes JSON slots (`Saved/SaveGames/<slot>.json`, format **version 3** since the audit of 2026-10-19); saving is only allowed
> outside combat and outside the modal dialogue window. Every load reopens the level and applies the save to the fresh world in one explicit
> **restore order** (12 steps, `USaveGameSubsystem::GetRestoreOrder`). The **state inventory** below says, per owner, what is saved, what is
> derived on purpose and what is still open. Old audit (2026-10-08): `docs/port/save_audit.md`.
>
> Key files:
> - `Core/SaveGameSubsystem.*` (the step table at the top of the .cpp), `Core/SaveGameRules.*` (pure: slot info parsing, policy, load flow),
>   `CodexSaveBridge`, `Core/MissionSessionSubsystem`; per-system hooks: `AEnemyCharacter::CapturePatrolSnapshot / RestorePatrolSnapshot`
>   (`EnemyCharacterSave.cpp`), `UTutorialSubsystem::CaptureSaveState / RestoreSaveState`, `UVehicleSubsystem::CaptureSave / RestoreSave`,
>   `UStealthLureSubsystem`, `UMinimapSubsystem`, `UTacticalHeatSubsystem::SaveLooseSources`, `UWaveSubsystem::RestoreRandomEvents`,
>   `UWaveVictorySubsystem::RestoreKillStats`, `UColdSurvivalComponent::RestoreColdState`, `AOperativeCharacter::RestoreKilledInAction`.
> - Tests `CodexTactics.Save.Policy.*` (7: BlockReason, NoAutosave, LoadFlow, Version, RestoreOrder, SlotInfo, DialogueBlock) + `Core.SaveGame.Rules`;
>   smokes `SaveRoundTripSmoke`, `SaveCoverageSmoke` / `SaveCoverageLakeSmoke` (live-world dump diff), `SaveLoadSmoke`, `TutorialSaveSmoke`, `PauseMenuSmoke`.
>
> Sections: Restore order | State inventory | How to add saved state | Robustness | Follow-ups | Old text (§6).
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Restore order (audit 2026-10-19)

`ApplySaveData` runs ONE table of steps (`SaveGameSubsystem.cpp`, `FSaveRestoreStepEntry Steps[]`); `USaveGameSubsystem::GetRestoreOrder()` returns the same
names and the unit test `CodexTactics.Save.Policy.RestoreOrder` pins both the sequence and the invariants below. While the steps run, `IsApplyingSave()` is
true and `WriteSave` refuses every save (a beat start fired by a restored step once wrote its checkpoint over the slot being loaded).

| # | Step | What it restores | Why here |
|---|---|---|---|
| 1 | `Reset` | crews out of the vehicles (`PrepareForLoad`), walks to objects / use orders / the loot window cancelled, every carry / placement cancelled | nothing of the pre-load world may carry over; the objects get their saved places in `World` |
| 2 | `Environment` | the day cycle / weather / snow cover (`game_state.environment` -> `UEnvironmentSubsystem::SetState`; the level keeps its own lock; 2026-10-22) | before anything that samples it: a wave start resolves its roster from the weather, the cold reads the weather / phase multipliers |
| 3 | `SquadTransfer` | the hand-over counts (`squad_transfer.handed_over`) | the tutorial's conditions (`heat_packs_given`) read them |
| 4 | `Tutorial` | progress, used-up volumes, locks, lock hints, skipped | BEFORE the squad is placed: a teleport into a volume meets its saved fired flag (no stale beat start, no checkpoint over the slot) and the locks are up before any restored order |
| 5 | `Squad` | Susanin (rescue / recruited / his record), every operative: `ResetTransientStateForLoad`, stance, place, stats, items, weapon, cold flags, F-key deployable, cover fire mode, **killed in action** (`ApplyKilledInAction`), the leader | the state steps below act on restored operatives |
| 6 | `GameState` | flow phase / wave / preparation left / ambush kind, event waves + breach + cold drain, kill statistics, leader, Solo + trap Solo (followers hold), postures, Commander Mode, formation; THEN the standing orders of every operative (`ApplyOperativeOrders`: defense line, cover slot, prep station) | `SetAutonomousSquadCombat` / `RefreshFormation` would undo an anchor or a cover set before them |
| 7 | `Quests` | the quest chain | a powered gate starts opening; `World` snaps the gate right after |
| 8 | `World` | crates (level ones matched by name, runtime ones - remains - recreated / removed), piles, loose heat sources, gates + the paced creep, dialogue triggers, every interactable (matched by name + class / dynamic ones recreated / removed) | the ground everything else stands on |
| 9 | `Vehicles` | transforms, hull, shells, crews back in their seats | needs the operatives (5) and the world (8) |
| 10 | `Enemies` | routes (a missing runtime route rebuilt), living enemies (matched / recreated / removed - dead stay dead), then every patrol / search / scent snapshot once everybody exists | an escort needs its leader; the search sweep needs the world |
| 11 | `Stealth` | lure cooldowns per operative | needs the squad |
| 12 | `UI` | minimap fog + panel | cosmetic, last |

Out of band: the pending-load flag (`UMissionSessionSubsystem::PendingLoadSlot`) is consumed BEFORE `LoadGame` runs; the load cover hides the level's own
start until step 12 is done; `OnSaveApplied` fires after the steps (smokes dump the world there).

## State inventory (audit 2026-10-19)

Verdicts: **SAVED** (written and restored), **DERIVED** (recomputed or legitimately default on a reopened level - the fresh world starts it; combat-only state
cannot exist in a save because saves are refused in combat), **GAP** (open; listed under follow-ups). Keys are the JSON keys; "op" = a `squad[]` entry.

### Operatives (`AOperativeCharacter` + components)

| State | Verdict | Key / where |
|---|---|---|
| place, yaw, stance | SAVED | op `pos`, `rot_y`, `current_stance` (stance first: the capsule moves) |
| health / max, cold level, fortitude, level / EXP / stat points, accuracy, luck | SAVED | op fields (`RestoreHealth`: no damage event) |
| weapon in hands, clip / reserve per weapon, extra ammo, bonus items | SAVED | `current_weapon_id`, `ammo_inventory`, `extra_ammo`, `bonus_items` |
| items (matches, deployables, medkits, food, grenades, heat packs, firewood, flares, stoves, stove gas), heat buffer | SAVED | op counts, `heat_buffer_left` |
| guard (T), own fire posture | SAVED | `is_guarding`, `posture_override` |
| **killed in action** (a fallen member lying as a corpse, out of the squad) + his remains crate | SAVED (v3) | `killed_in_action` (every KIA operative of the world is written, `current_health` 0) -> `RestoreKilledInAction`; the remains crate is a runtime crate (`crates.<name>.runtime` + class / pos / name / `corpse_loot`) |
| **frostbite flag, frozen weapon** (sticky, not a function of the cold level) | SAVED (v3) | `frostbitten`, `weapon_frozen` -> `UColdSurvivalComponent::RestoreColdState` (tier re-derived silently) |
| cold tier / speed multiplier | DERIVED | from the restored cold level |
| **F-key deployable** (`SelectedDeployType`), **cover fire mode** (N) | SAVED (v3) | `selected_deploy`, `cover_fire_mode` |
| **cover slot** (pressed against a wall out of combat) | SAVED (v3) | `cover` {slot, wall point / normal, height, top, edges, wall actor} -> `EnterCover` after the squad state |
| **defense line** (Commander Mode "hold the line at X") | SAVED (v3) | `defense` {actor, point} -> `USquadAutonomySubsystem::SetDefenseObjective` after the squad state |
| **preparation station** (where he stood when the preparation began) | SAVED (v3) | `prep_station` -> `UWaveVictorySubsystem::RestorePrepStation` |
| tactical anchor of a plain move order (leash pin) | DERIVED | re-anchored at the saved spot by Commander Mode; only the facing / radius differ |
| knockdown, panic / stress, rage, status-effect timers, reload / shoot timers, prone dive, vault, carry task, planned pause orders, quick fire, planned shots, targets, corner aim, shimmy, manual corner peek, montages, body offsets | DERIVED | combat / seconds-long; `ResetTransientStateForLoad` drops whatever an in-place load would carry |
| autonomy "primary weapon" memory (`PrimaryWeaponId` / on the auto sidearm) | GAP (minor) | re-learned from the weapon in hands: saved on the auto-drawn pistol, the autonomy treats the pistol as primary |
| selection, selection ring, silhouette, vision cone toggle | DERIVED | UI; the O key is session-only by user decision |

### Squad and recruits

| State | Verdict | Key |
|---|---|---|
| leader, Solo / trap Solo (followers holding), squad posture, Commander Mode | SAVED | `is_leader`, `game_state.is_solo_mode / is_trap_solo / squad_posture / autonomous_combat` |
| hand-over counts (tutorial share lesson) | SAVED | `squad_transfer.handed_over` |
| Susanin: recruited, rescue happened, his own record while waiting | SAVED | `susanin.*` (+ `killed_in_action` inside his record) |
| provoked flags, formation slots, selection group, pause plans, deferred shots | DERIVED | per fight / rebuilt |
| defense markers (`UDefenseMarkerSubsystem`) | DERIVED | drawn every tick from the restored defense directives |

### Game flow, waves, mission

| State | Verdict | Key |
|---|---|---|
| phase (mapped by `ResolveLoadFlow`), wave, combat unlocked, preparation time left | SAVED | `game_state.phase / current_wave_index / is_combat_phase_unlocked / preparation_time_left` |
| **ambush kind of a resumed fight** | SAVED (v3) | `is_ambush_fight` -> `RestoreForLoad(..., bAmbushFight)` (was written, never read back) |
| **event waves** (flank breach, Susanin rescue) + **breach done** | SAVED (v3) | `breach_wave`, `susanin_wave`, `breach_triggered` -> `UWaveSubsystem::RestoreRandomEvents` (a reopened level re-rolled them: the rescue could land on a wave already fought) |
| **cold drain of the rest between waves** | SAVED (v3) | `cold_drain_mult` -> `RestoreColdDrainMultiplier` |
| **kill statistics** (victory panel) | SAVED (v3) | `kill_stats` -> `UWaveVictorySubsystem::RestoreKillStats` |
| **horde flyover memory** of the level (2026-10-25: the flyover once per level, then only new enemy types) | SAVED | `horde_intro` {`wide_shown`, `shown_types` [EEnemyArchetype names]} -> `UHordeIntroSubsystem::RestoreMemory` (step 6; missing = empty: the level's first horde flies over) |
| **the rest between waves** (2026-10-25: it starts by itself, then the wave) | SAVED | phase `Preparation` + `preparation_time_left` -> `RestoreForLoad` resumes the same rest for wave 2+ (`bRestCountdown`); a save from the fight still resumes the wave at once |
| pause charges / cooldown, turn-based uses, free pause, horde clocks, horde intro, turn-based grid state, wave spawns | DERIVED | combat-only / full outside combat |
| mission intro seen, objective text, `bCombatFinished`, feed / floating-text history, run telemetry | DERIVED | a loaded save starts without the intro; the objective is recomputed; telemetry counts the run from the load (analytics only) |
| encounter "ambush started" counter, console overrides | DERIVED | the fight itself is in the phase / combat unlocked |
| **environment**: hour, weather, next weather, blend position (elapsed / dwell), weather RNG state, locked, **snow cover** (2026-10-22) | SAVED | `game_state.environment` {hour, weather, next_weather, weather_elapsed, weather_dwell, seed, locked, snow_amount} (`EnvironmentRules::StateToSaveJson / StateFromSaveJson`) -> restore step 2; the debug clock scale is DERIVED (1) |

### Enemies and AI

| State | Verdict | Key |
|---|---|---|
| living enemies: name, class, archetype, place, health, route, escort leader, patrol speed, placed-in-level marker | SAVED | `world_state.enemies[]` (+ `patrol_routes` for runtime routes); anyone missing from the save is removed (dead stay dead) |
| patrol progress (waypoint, direction, phase, wait), suspicion, search (origin, elapsed, lure / scent spec) | SAVED | `patrol.*` (`FEnemyPatrolSnapshot`) |
| **hound scent episode** (sniffing / following, timers, scent point) | SAVED (v3) | `patrol.scent_phase / scent_phase_s / scent_since_s / scent_point` |
| guard post of a placed enemy | DERIVED | the fresh level's BeginPlay re-runs `TryStartGuardPost` before the snapshot (`RestorePatrolSnapshot` keeps `bGuardingPost`) |
| lure "stand and listen" beat, same-noise stamp, search leg bookkeeping | DERIVED | re-walks to the origin; seconds |
| combat state (target, attack timers, fire fear, flank detour, morale, pounce), status effects, wave modifiers, marksman engage state | DERIVED | combat-only |
| lure stone / grenade in flight | GAP (negligible) | ~1 s window; the thrower's cooldown is kept |
| marksman legacy `PatrolIndex` (legacy point routes) | GAP (legacy maps only) | spline routes are saved |
| last-known silhouettes (`UTacticalSightSubsystem` ghosts), minimap enemy memory | DERIVED | fade within 12 s anyway |

### World objects

| State | Verdict | Key |
|---|---|---|
| every interactable by name + class: place, hidden, collision, trap (trapped / defused / attempts / warned), generator health / broken, barrel fire, mine (owner / revealed / arming), tripwire (anchors / arming / PMC / spotted), barricade + turret health, turret power / broken, note read, campfire / stove timer | SAVED | `world_state.objects.<name>` (deployables + tripwires recreated / removed) |
| **grenade-trap blast values** (4 m / 85 vs the class 3.5 m) | SAVED (v3) | `trap_damage`, `trap_radius` |
| crates (looted / defused / destroyed / exact stash), piles, loose heat actors (flares, puddles), gates + paced creep, dialogue triggers | SAVED | `crates` (+ runtime crates), `dropped_items`, `heat_sources`, `gates`, `gate_progress`, `dialogue_triggers` |
| vehicles: transform, hull, disabled, shells, crews + seats | SAVED | `vehicles[]` |
| APC repair in progress, drive order, MG heat, turret target, doors / engine | DERIVED (repair: GAP low) | the engine follows the crew; a repair under way must be re-ordered |
| campfire wind flag, puddle source label, flare thrower, barricade contact timer, disarm walk-ups, placement ghosts | DERIVED | cosmetic / seconds |

### Tutorial, UI, camera

| State | Verdict | Key |
|---|---|---|
| step states + progress + contributors (role spots), beat, locks, command history (precredit), confines, used-up volumes, lock hints, skipped | SAVED | `tutorial.*` (`UTutorialSubsystem::CaptureSaveState`; tutorial files are the tutorial agent's) |
| multi-hit trigger counts (`TriggerHits`), speech playback position, breather / remedial timers, focus operative, retry prompt | DERIVED (TriggerHits: GAP low, tutorial domain) | a speech replays from its first line; a pending `trigger.count` step restarts at 0 |
| minimap fog per floor, panel choice | SAVED | `world_state.minimap` (+ `UMissionSessionSubsystem::MinimapPanelMode` for the session) |
| camera zoom / yaw / pan, full map open, HUD drawer, banners, feed history, roof cutaway, exit arrows, camera zones | DERIVED | UI; the camera snaps to the restored leader |
| dialogue window in progress | SAVE REFUSED (v3) | `ESaveBlockReason::Dialogue` ("Saving is disabled during a conversation"): its trigger would be saved as used and the talk never finish |

## Level-to-level carry (campaign, 2026-10-22 d) - not a save

`UCampaignSubsystem` (GameInstance) carries the squad from a level left through the gate exit to the next campaign level (`CampaignCarry::Capture / Apply`,
the same per-operative fields as `SaveOperative`: level, EXP, unspent points, max health / luck / accuracy / fortitude, item counts by their save keys, bonus
items, ammo per weapon, extra ammo, the weapon in hands; full health on arrival, no cold / frostbite / orders / cover; the killed in action stay behind).
It is applied in `ACodexTacticsGameMode::SpawnSquad` on the arrival map only, kept until NEW GAME or the next transition (a restart of the arrival level
brings the squad back as it arrived) and is NOT written into save files: a save made on the arrival level holds the operatives' values itself, and a load
applies them over the spawned squad as before. Details: [demo01-lake](demo01-lake.md) block 2026-10-22 d.

## How to add saved state for a new feature

1. **Decide the verdict first** (write it into the inventory above): SAVED, or DERIVED with the reason (combat-only, recomputed, cosmetic). Saves are only
   written in Exploration / Preparation / WaveCleared / PostCombat with no dialogue open, and EVERY player load reopens the level: anything not saved comes
   back as the level starts it.
2. **Give the owner a save hook pair** on its own class / subsystem (`CaptureX / RestoreX`, or a plain setter `RestoreX` - no damage / UI events, no
   float texts; see `UColdSurvivalComponent::RestoreColdState`). Keep the JSON building in `SaveGameSubsystem.cpp`.
3. **Write the keys** in the matching section of `BuildSaveData` (`squad[]` entry via `SaveOperative`, `game_state`, `world_state.<section>`, `objects.<name>`
   via `SaveObject`, `enemies[]` via `SaveEnemy` / `FEnemyPatrolSnapshot`). Every key is OPTIONAL: older saves must load without it.
4. **Restore in the step that has its dependencies** (`GetRestoreOrder` table above): global state that later steps sample (the environment) in step 2, a per-operative value in `ApplyOperative` (step 5), a standing order in
   `ApplyOperativeOrders` (end of step 6, after Solo / Commander Mode / formation), world objects in step 8, enemy state in the snapshot (step 10), a per-squad
   subsystem after the squad (11), UI last (12). Read with `SaveInt / SaveFloat / SaveBool / SaveString / LoadVector / LoadEnumName` (missing key = the fallback
   you pass), never `GetXField` on a key that may be absent. A new ordered concern = a new row in the step table + the names list + the RestoreOrder test.
5. **Order inside a step**: ApplyOperative resets transient state and sets the stance BEFORE the teleport; anything that `RefreshFormation` /
   `SetAutonomousSquadCombat` / `SetSquadPosture` would undo goes AFTER them.
6. **Bump nothing unless the semantics change** (new optional keys do not need a version); when a version bump is due, document the key list at
   `SaveGameRules::CurrentSaveVersion` and update `CodexTactics.Save.Policy.Version`.
7. **Prove it**: a mutation in `SaveRoundTripSmoke::BuildWorld` (the saved-data diff) AND a line in `SaveCoverageSmoke::DumpWorld` + `BuildWorld` (the live
   dump diff, independent of the JSON - this is what finds a key you forgot). Run `smoke_parallel.ps1 -Commands SaveRoundTripSmoke,SaveCoverageSmoke,SaveCoverageLakeSmoke,TutorialSaveSmoke`.
8. **Docs**: the inventory row above, a CHANGELOG row.

## Robustness (audit 2026-10-19)

- **Corrupt / foreign / sparse files never crash**: `GetAllSaves` skips a file that is not JSON or holds none of `version / squad / game_state / world_state`
  (`SaveGameRules::ParseSlotInfo`, test `Save.Policy.SlotInfo`; a log line names the file); `LoadGame` refuses the same two with a warning; a sparse /
  version 1 / 2 file loads with the defaults of every missing key (a missing `world_state` logs a warning and leaves the world as the level starts it); a
  future version logs "loading what is known". Every read goes through the fallback helpers; enum names that do not parse keep the current value.
- **Version field**: `version` (3), `SaveGameRules::CurrentSaveVersion`; the key history is documented at the constant.
- **Missing map**: `FSaveSlotInfo::bMapAvailable` (`USaveGameSubsystem::IsSaveMapAvailable`, `FPackageName::DoesPackageExist`); the load list shows
  "MAP MISSING" after the level name (`CodexSaveBridge::ListSlots`); `LoadGameWithTravel` (F9, Continue, LOAD GAME, Retry) refuses it with a feed line
  instead of travelling to a level that does not exist; the slot stays listed so the player can delete it. Version 1 saves (no map) load on the current level.
- **No save while a load is applied**: `WriteSave` refuses while `IsApplyingSave()` (log line).
- **No save during a dialogue**: `ESaveBlockReason::Dialogue` in `CanSaveNow` (F5, SAVE GAME, the tutorial checkpoint all go through it).
- Smoke coverage: `SaveCoverageSmoke` writes a truncated file, a foreign JSON and a copy of the quicksave pointing at a missing map beside the real
  quicksave: the list skips / marks them, loads refuse them, F9 still picks the quicksave; afterwards a sparse version-1 file loads in place.

## Follow-ups left open by the audit

- **APC repair in progress** (`UVehicleSubsystem::Repairs`) is dropped by `PrepareForLoad`: the Engineer stands idle after a load, the hull keeps its saved
  HP; re-orderable (low).
- **Autonomy primary-weapon memory** (`USquadAutonomySubsystem` `PrimaryWeaponId` / `bOnAutoSidearm`): a save on the auto-drawn sidearm makes it the
  "primary" after a load (minor).
- **Tutorial `TriggerHits`** of a pending multi-hit step (tutorial agent's files): restarts at 0 after a load (low).
- **Lure stone / grenade in flight** at the save moment: lost (~1 s window; cosmetic); **marksman legacy `PatrolIndex`** (legacy point routes only).
- **Manual corner peek** is not restored with the cover slot (the slot is; a peek is a click away).
- The user's own saves from before version 3 keep loading (every new key is optional) - a quick manual check of one old slot in the editor is still worth doing.

## Save system (old §6)

### Save system (2026-10-08; audit `docs/port/save_audit.md`)

- `USaveGameSubsystem` (`Core/SaveGameSubsystem.*`) writes JSON slots (`Saved/SaveGames/<slot>.json`), format **version 2**
  (`SaveGameRules::CurrentSaveVersion`; version 1 / Godot saves still load — missing sections leave the world alone).
  Saved: squad (+ bonus items, per-operative fire posture), flow phase / wave / preparation time / ambush, squad posture,
  Commander Mode, quest chain, crates, piles, every interactable by actor name (position, hidden, trap, generator,
  barrel fire, mine, tripwire, barricade / turret health, turret power / broken, note read), gates (+ the paced creep), dialogue triggers,
  patrol routes, living enemies (patrol waypoint / phase / wait / search / escort leader; anyone missing = dead stays
  dead), Susanin (rescue + recruited). Deployables / tripwires / enemies / routes missing from the world are recreated
  from their class path; ones not in the save are removed.
- **Policy (user decision 2026-10-08):** `CanSaveNow(&Reason)` — saving only outside combat (Exploration, Preparation,
  WaveCleared, PostCombat); `QuickSave` / `SaveToSlotWithMessage` refuse in the fight / cutscene / game over with
  *"Saving is disabled during combat"*. `SaveGame` itself is unchecked (tests). **Rev 5f (user decision 2026-10-14): NO
  autosaves.** The only saves: the tutorial checkpoint (`UTutorialSubsystem::SaveCheckpoint`, slot `checkpoint`, at the start of
  a beat with `"checkpoint": true`; never while a load is pending, never in the fight), F5 (`quicksave`) and the pause menu's
  SAVE GAME (named slots). The old autosave (before a wave / ambush and after a wave was cleared, slot `autosave`) is removed;
  old `autosave*` files are hidden (`SaveGameRules::IsListedSaveType`), not deleted. F9 = the newest quicksave / checkpoint
  (`PickQuickLoadIndex`); Continue = the newest listed slot. The load list labels every slot (CHECKPOINT / QUICKSAVE / SAVE).
  Headless `-ExecCmds` runs write checkpoints only with `bCheckpointsEnabled` or CVar `Codex.Save.HeadlessCheckpoints 1`.
  Guard test `CodexTactics.Save.Policy.NoAutosave` (scans the game module: `SaveGame(` only in QuickSave / SaveToSlotWithMessage /
  SaveCheckpoint). Rev 5f also saves the tutorial's used-up volumes, running step progress, the hand-over counter (see §6 rev 5f).
- **Load:** `LoadGame` applies in place; `LoadGameWithTravel` (frontend CONTINUE / LOAD via `CodexSaveBridge`) stores the
  slot in `UMissionSessionSubsystem::PendingLoadSlot`, reopens `FSaveSlotInfo::MapName` and applies it 0.5 s after begin
  play (Game mode, no intro). `GetContinueSlot`, static `OnSaveApplied()`. Load flow: `SaveGameRules::ResolveLoadFlow`.
- **Rev 2026-10-19 (user: "the save keeps the state the character was in BEFORE the load - data gets mixed"; repro: a lesson
  knockdown -> Retry -> the Commander was still getting up from it): EVERY PLAYER LOAD REOPENS THE LEVEL.** Before: F9 / CONTINUE /
  LOAD of another map travelled, but **Retry from checkpoint** (`UTutorialSubsystem::RetryFromCheckpoint` -> `LoadGame`) and the pause
  menu's **LOAD GAME on the same level** (`CodexSaveBridge::LoadSlot` -> `LoadFromSlotWithMessage`) applied the save onto the LIVE
  world: transient state survived (the knockdown and its get-up montage + buffered order, cover / pending cover / shimmy / corner aim,
  panic stress, the autonomy anchors / targets, the prone dive's crawl-on, use orders, a box selection, the mesh offset) and Solo was
  only toggled when it differed. Now both call `LoadGameWithTravel` (black load cover, fresh world). **In-place loads remain only for
  dev / smokes** (`LoadGame` / `LoadFromSlotWithMessage` called directly) and for a Retry whose steps were injected at runtime (not the
  level's demo settings: a reload could not rebuild them - TutorialSmoke); for those `ApplySaveData` now resets first:
  `AOperativeCharacter::ResetTransientStateForLoad` (knockdown `UKnockdownComponent::ResetForLoad` - no get-up, no buffered order, no
  cover return; `UPanicComponent::ResetForLoad`; corner aim / cover / pending cover / shimmy / manual peek; the prone dive without its
  crawl-on; `TacticalAnchor` / autonomy target; `StopAllMontages`; `ResetBodyOffsets`), the interaction walk / use orders / loot window
  cancelled, Solo restored as ONE unit (`USquadSubsystem::RestoreSoloForLoad`: mode + trap Solo + followers holding, a multi-selection
  dropped, holding followers stopped). **Enemies (user: patrol speed 65 back to 190 after a load):** `SaveEnemy` writes
  `patrol_walk_speed` and `level_enemy` (`ULevelEncounterSubsystem::IsLevelEnemy`); a matched live actor keeps its own (map) values,
  a RECREATED one gets them back (`PatrolWalkSpeed`, `MarkLevelEnemy` - a recreated actor was not "placed in the level", so on a
  "button" level its contact never started the fight: the squad did not engage and, Attack.* still tutorial-locked, walked off in
  formation); an enemy the load removes is renamed out of the way first (`LoadDiscardedEnemy_N`) so the recreated one gets its saved
  name (it drifted to `_C_12`, the next save stored that and a later fresh level no longer matched its placed actor -> 190 again).
  Search / lure / scent end -> back on the route at `PatrolWalkSpeed` (unchanged). Smokes: `SaveRoundTripSmoke` (patrollers at 65:
  the recreated hound in place, all three after the reopened level, the searcher back on its route at 65 after the search),
  `TutorialSaveSmoke` (Retry reopens the level; knocked down -> Retry -> up, idle, no montage, mesh on the capsule, twice; Solo ON F5 ->
  F9 -> Solo + followers holding, a leader move leaves them; an Aggressive squad + a placed enemy's detection -> WaveCombat, Attack.*
  unlocked, the enemy shot, nobody panics), `PauseMenuSmoke` (LOAD GAME reopens the level).
- Tests `CodexTactics.Save.Policy.*` (4) + `Core.SaveGame.Rules`; smoke `SaveRoundTripSmoke` (exclusive; snapshot / save /
  mutate / load / diff in place and after travel, policy, no autosave, F9 pick, legacy file hidden) + `SaveLoadSmoke` +
  `TutorialSaveSmoke` (rev 5f: the review map with the demo tutorial - checkpoint, F5, F9 reopening the map, Retry).

