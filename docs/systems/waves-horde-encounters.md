# Game flow, waves, horde intro and encounters

> `GameFlow/*` runs the phases and time dilation; a wave starts suddenly (the horde intro opens a free tactical pause with a flyover). Enemies placed in a
> map start the ambush fight on contact (`ULevelEncounterSubsystem`); placed enemies without a route guard their post.
>
> Key files:
> - `GameFlow/*`, `Combat/WaveSubsystem`, `HordeIntroRules`, `HordeIntroSubsystem`, `GameFlow/LevelEncounterSubsystem`, `LevelEncounterRules`, `Content/Data/LevelJson/*.json`
> - Smokes `HordeSmoke`, `HordeIntroSmoke`, `LevelWaveSmoke`, `PatrolAmbushSmoke`, `WaveCombatSmoke`; Wave Editor: [tools-bot-waveeditor](tools-bot-waveeditor.md)
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Game flow / combat (old §5): phases, sudden horde, level enemies, guard posts

**Game flow / combat (partly Gemini)**
- `GameFlow/*` — phases Exploration → Cutscene → Preparation → WaveCombat (RealTime / TacticalPause / TurnBased) →
  WaveCleared / PostCombat / GameOver; time dilation 0.02 in the pause. Godot `main.gd` flags.
  **Sudden horde start (user decision + feedback 2026-10-11, default):** `FGameFlowConfig::bSuddenWaveStart` (+
  `PreparedWaveIndices` from the level JSON per-wave `"sudden_start": false`, `LevelFlowRules::ApplyLevel`) — `TriggerCombatZone` /
  `AdvanceAfterWave` go straight to `WaveCombat / RealTime` with the horde intro armed (`IsAwaitingHordeIntro`; no cutscene, no
  preparation / rest countdown): the wave spawns at once into the running fight. `EnterFreeTacticalPause` (the intro) opens the
  **free pause** (`IsFreePause()`: no charge, no timer, ends like any pause — Space tap / hold); a Space tap before the trigger
  opens it at once; `CancelHordeIntro` keeps real time (smokes, the bot). Console override `Codex.HordeIntro.SuddenStart -1|0|1`.
  A loaded save resumes in the wave's preparation and the next flow tick starts it suddenly. Ambush fights are unchanged (no
  horde intro). **Level enemies always start the fight (user bug 2026-10-11):** `ULevelEncounterSubsystem` records the enemies
  placed in the map at `OnWorldBeginPlay` (`IsLevelEnemy`, `LevelHasPlacedEnemies`; `MarkLevelEnemy` for smokes); "auto" is an
  ambush level when the map holds patrols OR any placed enemy; and on every level, even "button", any hostile contact with a
  placed enemy in exploration — it attacks an operative (`AEnemyCharacter::NotifySquadAttacked` from the blow, spit, pounce and
  the marksman's shot -> `EAmbushTrigger::EnemyAttack`), spots the squad, or is hit — starts the ambush fight
  (`LevelEncounterRules::ShouldStartFightOnContact`). Runtime / wave spawns on a button level do not. **Guard posts (user
  request 2026-10-11):** a placed enemy without a route of its own (spline / legacy marksman points) and without an escort
  leader guards its placement (`AEnemyCharacter::TryStartGuardPost` at BeginPlay / `MarkLevelEnemy`;
  `PatrolRouteRules::ShouldGuardPost`; per actor `bGuardPostWhenUnrouted`, default on): a patrol without a route - stands,
  faces its placement yaw with a look-around sweep (`GuardLookAroundDeg` 45 / `GuardLookPeriodSeconds` 12,
  `GetGuardLookOffset`), walks back after a search / shove (`ShouldWalkBackToPost`, 2 m / 0.8 m), uses the patrol
  perception (sight + suspicion, hearing by gait / gunshot / grenade, hound smell; trap -> search 60 s -> back to the post)
  and `BreakPatrol` -> the normal ambush flow. The marksman guards in `EMarksmanAIState::Patrol` (no more Engage at
  BeginPlay without a route - Sprint 06-D's rule now only for wave / runtime spawns); an escort mirrors its guarding
  leader (stays tethered). Wave / runtime spawns hunt as before. The
  squad gets the old warm / healed start and its post-combat stations at the wave-1 spawn (`UMissionSubsystem`, `UWaveVictorySubsystem`).

## Horde intro (old §5)

- `Combat/HordeIntroRules` + `HordeIntroSubsystem` (2026-10-11, reworked after the user's review) — timeline: (1) spawn, the fight
  runs in real time (`PreTriggerTimeDilation` 1), camera on the squad, normal RTS control (orders work; Esc / Space open the free
  pause at once, a click is a normal click); (2) trigger once the horde is a running mass: `MassMovingFraction` 0.6 of the live
  enemies run at the squad (>= `MassMinSpeed` 250 cm/s, heading within 60 deg) for `MassHoldSeconds` 0.4 (latched) and not before
  `MinDelaySeconds` 2.5 s, `MaxWaitSeconds` 6 s at the latest; (3) the free pause opens, the «HORDE INCOMING / TACTICAL PAUSE - GIVE
  YOUR ORDERS» alert (`UI/HordeAlertWidget`, 3 s + fade, restylable WBP subclass via `AlertWidgetClass`) and a slow-motion flyover
  on real time at `FlyoverTimeDilation` 0.2: wide shot of the running mass (moving anchor 80 % towards it, 20..40 m, seen from the
  squad's side turned 25 deg: `ATacticalCameraPawn::SetCinematicYaw`, 3 s), a close tracking shot (7 m, 2.5 s) of a running
  front-runner per type not shown in this fight (spawn order), the glide back to the leader (1 s, player yaw restored); (4) the
  world settles into the free pause (0.02). Known types only: a 2.5 s wide shot + return. Esc / click skip the flyover (camera
  snaps to the leader, free pause). Camera input locked meanwhile, camera zones step aside, the line of sight shows the whole
  horde (`UTacticalSightSubsystem::SetCinematicReveal`). Hooks for a later HUD effect: `OnHordeIntroStarted(Wave, bFlyover)` /
  `OnHordeIntroFinished(Wave, bSkipped)` (BlueprintAssignable). The bot orders its defences and dismisses the intro (real time).

## Sudden horde start data (old §6)

- **Sudden horde start data (2026-10-11).** Level JSON per wave `"sudden_start"` (bool, default true when missing; Wave Editor
  card button «ВНЕЗАПНАЯ АТАКА» / «С ПОДГОТОВКОЙ»): false keeps the old start of that wave. `prep_phase_duration` /
  `wave_rest_duration` stay parsed but only matter for such waves. No level sets it yet, so every horde wave starts suddenly.
  Tuning is console-only for now (`Codex.HordeIntro.*`): SuddenStart -1, Enabled -1, PreTriggerTimeDilation 1, MassMovingFraction
  0.6, MassMinSpeed 250, MassHoldSeconds 0.4, MinDelaySeconds 2.5, MaxWaitSeconds 6, FlyoverTimeDilation 0.2, WideShotSeconds 3,
  RepeatWideShotSeconds 2.5, TypeShotSeconds 2.5, ShotBlendSeconds 0.7, ReturnBlendSeconds 1, TypeShotDistance 700,
  WideMinDistance 2000, WideMaxDistance 4000, WideFocusBias 0.8, ViewSideDegrees 25, AlertSeconds 3. Each wave logs a
  `[HordeIntro] N s: ... moving, mean ... cm/s, ... % running at the squad` trace for tuning.

