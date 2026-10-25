# Game flow, waves, horde intro and encounters

> `GameFlow/*` runs the phases and time dilation; a wave starts suddenly (the horde intro opens a free tactical pause; the camera flyover only for the
> level's first horde and for enemy types never shown on the level, 2026-10-25); between waves the rest runs and the next wave comes by itself (no
> "Start the next wave", 2026-10-25). Enemies placed in a
> map start the ambush fight on contact (`ULevelEncounterSubsystem`); placed enemies without a route guard their post.
>
> Key files:
> - `GameFlow/*`, `Combat/WaveSubsystem`, `HordeIntroRules`, `HordeIntroSubsystem`, `GameFlow/LevelEncounterSubsystem`, `LevelEncounterRules`, `Content/Data/LevelJson/*.json`
> - Smokes `HordeSmoke`, `HordeIntroSmoke`, `LevelWaveSmoke`, `PatrolAmbushSmoke`, `WaveCombatSmoke`; Wave Editor: [tools-bot-waveeditor](tools-bot-waveeditor.md)
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Red horde-direction arrows (game designer 2026-10-25)

"We need a visual indicator of the direction from which the horde is moving - the same arrows as the tutorial's, in RED; several hordes = several
arrows." `UI/HordeArrowsWidget` (HUD layer 13, just under the tutorial overlay; `ACodexTacticsHUD::GetHordeArrows`, class `HordeArrowsWidgetClass`):
the guide arrow's look (a 20 px chevron on the screen edge, the distance in metres from the leader) in red. **Grouping = by bearing**, not by lane
name (`UI/HordeArrowRules`: `GroupByBearing`): the fight's live enemies (`UWaveSubsystem::GetAliveEnemies`, never the level's parked patrols) are
sorted by bearing from the leader; a gap of `MergeAngleDeg` (35 deg) or more splits two groups, so two lanes = two arrows and a lane split over two
spawn points = one. Enemies within `HideRangeCm` (18 m, engaged) are not counted. A group's arrow shows only while its centroid is off-screen (the
guide arrow's rule), in the WaveCombat phase (turn-based fights too: the guide arrow is not hidden there either); dead group = no arrow. Cost:
regroup 10 Hz (`RefreshIntervalSeconds`), the projection every frame, no world scans. The three tunables are UPROPERTYs of the widget (a Widget
Blueprint subclass / the HUD class sets them). Tips are spread (`SpreadTips`, 56 px) so two red arrows or a red + the amber guide arrow never sit on
one spot. Tests `CodexTactics.UI.HordeArrows.Groups / SpreadTips`; smoke `HordeArrowsSmoke` (`shot` argument: a rendered still).
## Waves break through on their own: no "Start the next wave" (game designer 2026-10-25)
"When a wave ends, remove the plate with the text «Start the next wave». The waves must break through on their own."
`FGameFlowStateMachine::NotifyWaveCleared` of a wave that is NOT the fight's last (`IsFinalFight` false) now goes **WaveCombat -> the rest
before the next wave** (`BeginRestBeforeNextWave`: `WaveIndex + 1`, `Preparation`, `PreparationTimeRemaining = WaveRestDuration` = level
JSON `wave_rest_duration` (20 s on every level), pause charges back, the world runs - no `WaveCleared` phase, no victory panel, time never
stopped). When the rest runs out (`Tick`) the wave starts by itself: a sudden wave (`UsesSuddenStart`) awaiting its horde intro, a prepared
one (`"sudden_start": false`) in real time. A sudden wave counts the rest down too (`bRestCountdown`; before, a sudden wave never had a rest:
it started on the click). The last wave still goes straight to PostCombat (2026-10-22), a single ambush fight too, skirmishes to Exploration.
| Where | What changed |
|---|---|
| `FGameFlowConfig::bVictoryPanelBetweenWaves` | default false; true restores Godot's panel between waves (`WaveCleared`, time stopped, «Start the next wave (N/M)» -> `AdvanceAfterWave`). |
| HUD (`UPhaseBannersWidget`) | the «Start Battle» button only in the preparation before the FIRST wave (`OffersStartBattle`: a level's own prepared start stays as designed); in the rest the combat banner reads `NEXT WAVE IN: NN sec` (`IsRestBetweenWaves`), no button. The victory panel (`UVictoryPanelWidget`) only shows in `WaveCleared`, which the default flow never reaches. |
| Feed / mission | "Wave N cleared. Regroup!" (`UWaveVictorySubsystem`, also on WaveCombat -> Preparation), the HQ rest radio "Prepare for the next wave! You have N seconds to regroup." and the objective "PREPARATION: N sec until wave M" (`UMissionSubsystem`, unchanged - they already belonged to the rest). |
| Fire posture | `FirePostureRules::ClearsProvocation(Preparation)`: the fight's provocation ends in the rest (it ended on the panel before). |
| Saves | a save in the rest (allowed: Preparation) stores `preparation_time_left`; the load resumes THAT rest (`RestoreForLoad`: wave 2+ with seconds left -> `bRestCountdown`), then the wave comes by itself. A save from the fight still resumes the wave at once (sudden) as before. |
| Bot / smokes | `FinishPreparation` still cuts the rest short (the bot does, as it used to click at once); the HUD no longer offers it there. |
| Unchanged | the first fight start per level (the lake: the console quest `QuestStep.GatePowered` -> `combat_start: "button"` level, sudden wave 1; a level with a prepared start keeps cutscene + «Start Battle»), the gate (`IsFinalWaveCleared` only after the final wave; in the rest the wave index is already the next one, like the old rest), the exit flows (PostCombat), the tutorial (no step waited for the button - checked `demo_01_steps.json`). |
Tests `CodexTactics.GameFlow.AutoNextWave.*` (`ClearedWaveRestsThenTheNextBreaksThrough`, `PreparedStartKeepsItsFirstButtonOnly`,
`AmbushPanelOptionAndRestRules`), `CodexTactics.Combat.HordeIntro.FreePause.NextWaveComesSuddenlyAfterTheRest` / `.LoadedPreparationStartsTheWaveSuddenly`,
`GameFlow.FightEnd.LastWaveNoPanel`; the classic `GameFlow.Phases.*` tests run with the Godot panel on. Smokes: `HordeIntroSmoke` stage 4-5 (wave 1 cleared ->
rest, no panel / button / `WaveCleared`, banner "NEXT WAVE IN", nobody clicks, wave 2 breaks through after the 20 s rest), `GateExitLakeSmoke` (the lake's two
rests run out by themselves, then the exit), `VictorySmoke`, `GateGeneratorSmoke`, `SaveRoundTripSmoke` (F5 in the rest resumes the rest).
## The horde flyover only once per level (game designer 2026-10-25)
"When the horde approaches, the camera flyover must happen only once. It shows the first wave, shows its enemies, and that's it. All
following waves are no longer shown. Only if there are new unique enemies, show that new unique enemy. Nothing else is shown."
`UHordeIntroSubsystem` keeps a per-LEVEL memory (`FHordeIntroMemory`: `bWideShotShown`, `ShownTypes`; it was a per-fight set reset in
Exploration / the first preparation). `HordeIntroRules::BuildShotList(Types, Memory, Config)`:
| Wave | Flyover |
|---|---|
| the level's first horde (no wide shot flown yet) | wide shot of the running mass + a close shot per type (spawn order, `MaxTypeShots`) + return - as before |
| later, with a type never shown on this level | ONLY that type's close shot(s) + return - no wide shot |
| later, every type known | no flyover at all |
`HordeIntroRules::RememberShots` marks the wide shot / the types when a flyover STARTS (a skip by Esc / a click / Space counts as shown); a free pause
opened before the trigger (Space / Esc) flies nothing and remembers nothing. `RepeatWideShotSeconds` (and `Codex.HordeIntro.RepeatWideShotSeconds`) is gone.
**Kept on every sudden wave (choice 2026-10-25):** the trigger when the horde becomes a running mass, the «HORDE INCOMING / TACTICAL PAUSE - GIVE YOUR ORDERS»
alert, the HQ feed line and the FREE tactical pause (no charge, no timer) - the designer named only the camera; the pause is the player's planning moment of
every wave and the alert says why time stopped. `OnHordeIntroStarted(Wave, bFlyover)` fires with `bFlyover` false for a wave without a flyover.
**Saved:** `game_state.horde_intro` {`wide_shown`, `shown_types` [enum names]} (`USaveGameSubsystem`, restore step 6 GameState; older saves: empty = the
level's first horde flies over again) - a load / checkpoint retry / F9 never repeats a flyover already seen; a level restart or the next campaign level
starts with an empty memory. Tests `CodexTactics.Combat.HordeIntro.ShotList.WideOnceThenOnlyNewTypesOnThisLevel`; smokes `HordeIntroSmoke` (wave 1: wide +
hound + frostbitten; wave 2: only the new cutter / marksman shots, no wide; wave 3 of known types after a load: alert + free pause, NO flyover),
`SaveRoundTripSmoke` / `SaveCoverageSmoke` (the memory round trip).

## Skirmishes and the Susanin switch (2026-10-21)

User play-test of the Demo 01 A4 ambush: "if the first or the second zombie attacks and we kill it, remove the next-wave option - they attack
us, we kill them and play on, as smooth as possible, no waves at this stage" and "remove Susanin appearing during the fight". Approved: the
placed-enemy fights of the lake (A4, the A5 base) are skirmishes; the A6 scripted defence waves keep their panel.

| Level JSON key | Values | Effect |
|---|---|---|
| `"ambush_fight"` | `"level"` (default) / `"skirmish"` | `FLevelCombatConfig::bAmbushSkirmish` -> `FGameFlowConfig::bAmbushSkirmish` (`LevelFlowRules::ApplyLevel`). A skirmish = the ambush fight (`StartAmbushCombat`) of only the enemies that ENGAGED: the contact's source (`ULevelEncounterSubsystem::GetLastContactSource`) + every level enemy that broke its patrol and is not back on it (its escort / leader break with it) - `UWaveSubsystem::AdoptSkirmishEnemies`; the others keep patrolling. A level enemy that engages later (sight, hearing the fight, being hit) joins it (`NotifyHostileContact` -> `UWaveSubsystem::JoinSkirmish`). No `OnWaveStarted` (no wave objective, no Susanin, no flank breach), the combat banner reads `CONTACT \| HOSTILES: n`, the feed "Contact!". When the last of them dies: `FGameFlowStateMachine::FinishSkirmish` = WaveCombat (any sub-mode) -> **Exploration at once** (no WaveCleared panel, no PostCombat teleport / enemy cleanup, no wave XP; wave index 0, the combat zone free again for the next skirmish), the feed "Clear. Carry on - quietly."; a skirmish whose only enemy died from the very shot that started it ends on the next `UWaveSubsystem::Tick` (never inside the flow's own broadcast). Scripted waves are never skirmishes. |
| `"susanin_wave"` | `0` = never, `N` = that wave, no key = random among 1-3 (Godot) | `FLevelCombatConfig::SusaninWave`, applied in `UWaveSubsystem::SetLevelConfig`; `URecruitSubsystem::HandleWaveStarted` also never fires in a skirmish. The feature stays (the user will say where to place it). |

Also (every level): the victory panel's button of a single ambush fight (`bAmbushSingleFight`, "level" mode) read "End combat and continue
exploring" (2026-10-22: that panel is gone, see "Fight end" below) (it was "Start the next wave (2/3)" while it led to PostCombat anyway). The tutorial's lesson pause: `FGameFlowStateMachine::EnterLessonPause`
= RealTime -> the FREE tactical pause (no charge, no timer) in any wave fight (`UTutorialSubsystem` action `tactical_pause`). 2026-10-22: the edges of the
tactical pause are gameplay events `TacticalPauseStarted` (+ `.Free` first) / `TacticalPauseEnded` (`UGameFlowSubsystem::HandleStateChanged`) - the tutorial's pause lesson.
Data: `Content/Data/LevelJson/L_Demo01_A_FrozenLake.json` (`"ambush_fight": "skirmish"`, `"susanin_wave": 0`), schema
`Content/Data/Schemas/level_config.schema.json`, Wave Editor `types.ts`. Tests `CodexTactics.GameFlow.Skirmish.LevelJson / Flow`; smokes
`TutorialAmbushLakeSmoke` / `TutorialAmbushLakeSpottedSmoke` / `TutorialAmbushLakeLureSpottedSmoke` (each fight ends straight in exploration, the
other zombie keeps patrolling), `AmbushSmoke`, `PatrolAmbushSmoke`, `LevelWaveSmoke`, `SusaninSmoke`.

## Fight end: no panel, no button (game designer 2026-10-22)

"After a fight ends, remove the 'End combat and continue exploring' text / button - it's not needed." `FGameFlowStateMachine::NotifyWaveCleared`:
when the cleared wave is the fight's last (`IsFinalFight`: wave >= `TotalWaves`, or an ambush fight with `bAmbushSingleFight`) the flow goes
**WaveCombat -> PostCombat at once** (time keeps running, the squad stays put, enemies / deployables cleaned up, victory dialogue in the feed,
HQ "all N waves repelled" line - `UWaveVictorySubsystem` posts it on that path too). `FGameFlowConfig::bVictoryPanelAfterLastWave` (default
false) restores the Godot panel. Skirmishes were already panel-less (straight to Exploration). The panel BETWEEN waves of a
multi-wave fight ("Start the next wave (N/M)") stayed until 2026-10-25 - gone too since (section "Waves break through on their own" above). What other levels rely on:
PostCombat itself (victory dialogue, "after victory" objective, the gate's "final wave cleared" latch, the bot's VICTORY, saves) - all reached
the same way; only the kill-statistics card of the last wave is no longer shown. The level end through the open gate: [demo01-lake](demo01-lake.md)
block 2026-10-22 c. Tests `CodexTactics.GameFlow.FightEnd.LastWaveNoPanel`; smokes `VictorySmoke`, `GateGeneratorSmoke`, `HordeIntroSmoke`, `GateExitLakeSmoke`.

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
  world settles into the free pause (0.02). Known types only: a 2.5 s wide shot + return. **Superseded 2026-10-25:** the memory is the
  level's (not the fight's), the wide shot only on the level's first flyover, later waves only a new type's shot, known types no flyover
  (section "The horde flyover only once per level" above). Esc / click skip the flyover (camera
  snaps to the leader, free pause). Camera input locked meanwhile, camera zones step aside, the line of sight shows the whole
  horde (`UTacticalSightSubsystem::SetCinematicReveal`). Hooks for a later HUD effect: `OnHordeIntroStarted(Wave, bFlyover)` /
  `OnHordeIntroFinished(Wave, bSkipped)` (BlueprintAssignable). The bot orders its defences and dismisses the intro (real time).

## Sudden horde start data (old §6)

- **Sudden horde start data (2026-10-11).** Level JSON per wave `"sudden_start"` (bool, default true when missing; Wave Editor
  card button «ВНЕЗАПНАЯ АТАКА» / «С ПОДГОТОВКОЙ»): false keeps the old start of that wave. `prep_phase_duration` /
  `wave_rest_duration` stay parsed but only matter for such waves. No level sets it yet, so every horde wave starts suddenly.
  (2026-10-25: `wave_rest_duration` is now the rest between ANY two waves - the next wave comes by itself when it runs out.)
  Tuning is console-only for now (`Codex.HordeIntro.*`): SuddenStart -1, Enabled -1, PreTriggerTimeDilation 1, MassMovingFraction
  0.6, MassMinSpeed 250, MassHoldSeconds 0.4, MinDelaySeconds 2.5, MaxWaitSeconds 6, FlyoverTimeDilation 0.2, WideShotSeconds 3,
  (RepeatWideShotSeconds removed 2026-10-25) TypeShotSeconds 2.5, ShotBlendSeconds 0.7, ReturnBlendSeconds 1, TypeShotDistance 700,
  WideMinDistance 2000, WideMaxDistance 4000, WideFocusBias 0.8, ViewSideDegrees 25, AlertSeconds 3. Each wave logs a
  `[HordeIntro] N s: ... moving, mean ... cm/s, ... % running at the squad` trace for tuning.

### Fight over: the squad stays put; horde fly-in framing (game designer play-test 2026-10-22)

- **No return to the preparation spots.** `UWaveVictorySubsystem::StartPostCombatSequence` no longer teleports the squad (Godot
  `_return_squad_to_prep_stations`; deviation by the designer: "when a fight ends the operatives must stay where they were"). Orders dropped,
  enemies removed, deployables recovered, the commander becomes the leader as before; the stations are still recorded (save games). After
  PostCombat / a skirmish ending in Exploration `USquadSubsystem` holds the followers (stance, cover) until the leader moves
  (`SquadOrderRules::ArmsPostFightHold` / `KeepsPostFightHold`, `IsPostFightHoldActive`). Smokes: `VictorySmoke` (scattered squad stays 3 s later),
  the TutorialAmbushLake skirmishes.
  2026-10-23: Regroup (B) ends the hold too, and the formation runs in PostCombat (it lasts until the level ends) - see
  [squad-and-orders](squad-and-orders.md) "After the final fight: Regroup brings them back".
- **Horde fly-in frames the wave.** `UHordeIntroSubsystem::GatherEnemies` = the fight's own enemies (`UWaveSubsystem::GetAliveEnemies`; the level's
  patrols / parked enemies never count). The wide shot frames the main cluster (`HordeIntroRules::MainCluster`, `WideClusterRadiusCm` 15 m) at the
  shortest distance that holds every runner's feet and head in the tactical camera's frustum (`FrameDistance` / `FrameWide`, `WideFrameMargin` 0.12,
  `FramedBodyHeightCm` 160, `WideMinDistance` 15 m .. `WideMaxDistance` 45 m; the squad-side focus when the cluster still fits from there, else the
  cluster's centre), the yaw fixed per shot, focus + distance follow the running cluster (`ATacticalCameraPawn::SetCinematicDistance`). Type shots
  prefer visible runners of the wave. The trigger waits while the wave has queued spawns or nobody alive (`ShouldTriggerSpawned`, `SpawnGraceSeconds`
  6). Checks: `CodexTactics.Combat.HordeIntro.Framing.WaveClusterInTheFrustum`, `.Trigger.WaitsForTheSpawn`; `HordeIntroSmoke` (a parked level hound
  60 m away is never filmed; per wide shot the main cluster in frame, >= 60 % of the wave, none hidden, the smallest >= 2.5 % of the frame height; per
  type shot the runner in frame >= 12 %).
