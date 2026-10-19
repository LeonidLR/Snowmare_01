# Save / load

> `USaveGameSubsystem` writes JSON slots (`Saved/SaveGames/<slot>.json`); saving is only allowed outside combat. Since 2026-10-19 every load reopens the
> level and nothing of the old world is kept. Audit: `docs/port/save_audit.md`.
>
> Key files:
> - `Core/SaveGameSubsystem.*`, `CodexSaveBridge`, `Core/MissionSessionSubsystem`; tests `CodexTactics.Save.Policy.*`, `Core.SaveGame.Rules`; smokes `SaveLoadSmoke`, `SaveRoundTripSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

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

