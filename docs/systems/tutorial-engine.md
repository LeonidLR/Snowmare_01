# Tutorial engine

> UE-only (Godot had no tutorial). Every player command passes `Core/PlayerCommands.h` (~60 ids) so beats can lock / unlock them; the pure
> `TutorialRules` + `TutorialStateMachine` drive hint cards, objectives and radio lines from `Content/Data/Tutorial/*.json`. The content of the actual
> Demo 01 flow is in [demo01-lake](demo01-lake.md).
>
> Key files:
> - `Core/PlayerCommands.h`, `Tutorial/TutorialRules`, `TutorialStateMachine`, `TutorialSubsystem`, `TutorialTriggerVolume`, `TutorialMarkerActor`, `UI/TutorialOverlayWidget`
> - `Core/DemoModeSubsystem`; data `Content/Data/Tutorial/<file>.json`; tests `CodexTactics.Tutorial.*`; smoke `TutorialSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Role-specific tutorial spots (old §5)

- **Role-specific tutorial spots (rev 5j, every level; the A3 cover lesson):** completion key `"role_spots": true` (`FTutorialCompletion::bRoleSpots`): an event counts only when its subject stands at HIS role's spot of the step = a `ATutorialMarkerActor` with that `StepId` and `RoleId` (the `EOperativeRole` name: `Commander` / `Engineer` / `MedicSapper`), within its `RingRadiusCm` + `RoleSpotToleranceCm` 80 (`UTutorialSubsystem::IsAtRoleSpot`, `FTutorialStateMachine::RoleSpotProvider`); `UTutorialSubsystem::CountRoleSpotPresence` (tick) also counts a member already in cover there (`HandleRoleSpotPresence`: only that step, its filters, never other steps on the same event); a role's ring hides once he counted (`IsRoleSpotDone`); a level with no role rings for the step: any place counts. Marker `Label` (FText) is drawn over the beacon by the tutorial overlay (`UTutorialOverlayWidget::MarkerLabels`, the marker's colour). `A3_cover`: `"role_spots": true` (text: the rings / roles); `A3_peek` completes at `traps_spotted.A3PeekMines` >= 4 (all four, found by the medic's cone). Map: additive section `5j` (`docs/demo/DEMO01_AUTOLAYOUT.md` §11). Tests `CodexTactics.Tutorial.RoleSpots.*` (Parse, EachAtHisOwnSpot, NoSpotsAnyCover); TutorialSmoke stage 115 (runtime rings: another role's ring does not count, own ring counts once, ring hides, any order).

## Tutorial framework (old §5)

**Tutorial framework (Demo 01 Sprint 1, C2 / C7 / C8 / C9 / C10, 2026-10-11)** — UE-only (Godot had no tutorial).
- **Command choke point** `Core/PlayerCommands.h`: ~60 logical command ids (`Order.Move`, `Squad.Solo`, `Squad.Regroup`,
  `Stance.Crouch`, `Stance.Squad.Prone`, `Cover.Enter`, `Cover.Shimmy`, `Cover.Corner`, `Object.Relocate`, `Object.LightBarrel`,
  `Item.HeatPack`, `Loot.Open`, `Trap.DisarmMine` / `DisarmObject` / `DisarmTripwire`, `Trap.SetOnObject`, `Deploy.Mine` /
  `Barricade` / `Turret` / `Tripwire`, `Attack.TargetedShot`, `Grenade.Aim`, `Time.Pause` / `Resume` / `TurnBased` / `RealTime`,
  `TB.EndTurn` / `TB.Attack`, `Posture.*`, `System.QuickSave`, camera ...) + a registry (default key label, section, description:
  the F1 Controls screen is generated from it: `PlayerCommands::GetSections()` order, `GetKeyLabel()` honours `SetKeyLabelOverride()` for rebinds, `CodexControlsRules::BuildSections()`; texts live ONLY in `PlayerCommands.cpp`). `PlayerCommands::Report(Context, Id, FPlayerCommandContext)` -> `UCodexEventBus::OnPlayerCommand`.
  Reported (one line each, behaviour unchanged) from `ACodexTacticsPlayerController` handlers (`ControllerCommands::Report`),
  `UInteractionSubsystem` (menu confirm -> light / defuse / use, loot, set trap), `URelocationSubsystem` (relocate / deploy /
  tripwire orders), `ATacticalCameraPawn` (WASD / edge pan start). Gameplay events `GameplayEvents::Raise` ->
  `UCodexEventBus::OnGameplayEvent`: `RelocationDone`, `RelocationRefused`, `BarrelLit`, `LessonDowned`, `TripwireSpotted` /
  `TripwireDisarmed` / `TripwireTriggered` (subject = the operative), cover (`AOperativeCharacter`, all modes: exploration, real
  time, pause execution, turn-based grid): `CoverEntered` (+ `CoverEntered.High` / `.Low`) once he has SETTLED at the slot (run-in
  glide / enter clip blend over, corner snap done, still; not on the order, not for the quiet return after an open shot), `CoverLeft`,
  `CoverShimmy` (a player's side-step; the automatic corner snap is none), `CornerReached` (kind Left / Right). Details for
  Blueprints / tools: `GameplayEvents::RaiseWithContext` -> `UCodexEventBus::OnGameplayEventDetailed(Name, FGameplayEventContext)`
  (Subject / SubjectId = the operative, Target / TargetId = the wall actor, Kind = High / Low / Left / Right, Location = slot);
  the tutorial also maps
  `OnMineSpotted` -> `MineSpotted`, `OnItemUsed` -> `ItemUsed` (+ `.ItemId`), `OnSoldierDowned`, `OnDialogueFinished`, cold tier ->
  `ColdTier` (+ `.Chills` / `.Freezing` / ...), flow -> `CombatStarted` / `CombatEnded`.
- `Tutorial/TutorialRules` (pure: steps JSON parse + validation, matching, line timing, lesson rule, save / telemetry formats),
  `Tutorial/TutorialStateMachine` (pure per-step machine: Idle -> Waiting (delay) -> Queued -> Active -> Completed / Skipped /
  TimedOut; one hint card at a time + 1.5 s breather, <= 3 objectives, one radio / walk-and-talk step; queued steps complete
  silently; once / replay; failure_retry rate-limited 60 s; export / import progress), `Tutorial/TutorialSubsystem`
  (UTickableWorldSubsystem glue: loads the steps file named by the level's demo flags on the first tick, real-time ticking,
  overlay views, markers, speech playback, checkpoint autosave per beat (slot `checkpoint`, only when `CanSaveNow`), lesson rule via
  `UHealthComponent::LethalDamageGuard` (HP floor 1 + `ForceKnockDown` + retry prompt), "Retry from checkpoint" = in-place
  `LoadGame("checkpoint")`, save section `"tutorial"` (progress applied later if the steps load after the save), telemetry
  `Saved/Tutorial/tutorial.jsonl`, ON / OFF `Codex.Tutorial.Enabled` + `Saved/Tutorial/profile.json`; dev console
  `Codex.Tutorial.Skip / Reset / Retry / Step <id>`), `Tutorial/TutorialTriggerVolume`, `Tutorial/TutorialMarkerActor`,
  `UI/TutorialOverlayWidget` (HUD z 14). Texts: `FNarrativeManifest::GetTutorialText` (manifest `"tutorial"` section, else a
  `sequences` entry, else `[EN missing: <id>]`), `UTutorialSubsystem::GetTutorialText` for Blueprints.
- **Demo game-mode flags** `Core/DemoModeSubsystem` + `FDemoModeSettings` (level JSON `"demo"` object, `LevelJsonRules::ParseDemoSettings`):
  Panic off on every operative, vision cones forced on / off on operatives and enemies (also later spawns), tutorial on + steps file,
  demo HUD preset (`ACodexTacticsHUD::bDemoHudPreset`: debug squad panel and operative labels hidden). No `"demo"` object = no change.
- **Command locks / conditions / actions (2026-10-12, Demo 01 A0-A1 redesign):** beats and steps carry `lock` / `unlock`
  (command id patterns; beat: on start, step: when shown or, never shown, when finished) -> `FTutorialStateMachine::IsCommandLocked`
  / `GetLocks` (saved as `"locks"` in the save's tutorial section; older saves rebuild them). The lock choke point for order handlers is
  `UTutorialSubsystem::CheckCommandLocked` / `FilterCommand` (`TutorialRules::ResolveLockedCommand`: a locked `Order.Sprint` degrades
  to `Order.Move`, other locked ids are refused) - used by `ACodexTacticsPlayerController::HandleWorldHit` (double click on the ground /
  on an object); a refusal raises `Tutorial.Locked` + `Tutorial.Locked.<CommandId>` (no error line). Tutorial OFF / no steps = no locks.
  Trigger `condition` (polled each tick) + step `condition` clauses (`squad_cold_max` / `squad_cold_min` / `leader_cold` /
  `squad_heat_packs`, `UTutorialSubsystem::GetConditionStat` via `FTutorialStateMachine::StatProvider`; a due step whose condition
  fails is skipped silently, followers go on), `trigger.requires` (arming steps), step `actions` (`cold_bump`:
  `UColdSurvivalComponent::AddColdShock` = `ColdRules::ApplyColdShock`, run when shown), `focus: coldest_operative` (card names / marks
  him, `{focus}` token, `FTutorialCardView::FocusOperative`), kind `feed` (message-feed line, no slot, completes at once). New event
  `ItemLooted` + `ItemLooted.<Item>` (`GameplayEvents::RaiseItemLooted`; `ALootCrateActor::TakeItem` = click / Take all, and the
  crate / pile drag in `USquadTransferSubsystem`). Level JSON `squad_loadout.heat_packs_count` (`FSquadLoadout::HeatPacksCount`,
  `LoadoutRules::StartingHeatPacks`, applied at spawn by the game mode; -1 = the tactical_heat.json role kit).

## Tutorial tests (old §5)

- Tests `CodexTactics.Tutorial.*` (9 + 8 + 2: `Lock.Rules`, `Lock.Schema`, `Lock.LockUnlock`, `Lock.Saved`, `Action.GustTimeout`,
  `Condition.Trigger`, `Schema.DemoFileA0A1Flow`, `Demo.HeatPackLoadout`, `Precredit.Rules`, `Precredit.StateMachine`), smoke
  `TutorialSmoke` (exclusive; runs the real demo file A0 -> A1), `SaveRoundTripSmoke` covers the "tutorial" save section.

### Tutorial: how the user places volumes / markers and edits steps (Demo 01, 2026-10-11; rev 5 2026-10-13; rev 5b 2026-10-13; rev 5c 2026-10-13; rev 5d 2026-10-14; rev 5e 2026-10-14; rev 5f 2026-10-14; rev 5g 2026-10-14; rev 5h 2026-10-15; rev 5i 2026-10-15)

## How to turn the tutorial on, place volumes / markers, edit steps and texts (old §6)

1. **Turn it on for a level**: in its level JSON (`Content/Data/LevelJson/<level>.json`) add
   `"demo": { "panic": false, "vision_cones": true, "tutorial": true, "tutorial_steps": "demo_01_steps.json", "hud": "demo" }`
   (every key optional; no `"demo"` object = normal game; `intro_dialogue` defaults to false when a `demo` object exists). **Which level JSON a map gets (2026-10-12):** `-LevelJson=` on the command line wins; else `Content/Data/LevelJson/<MapName>.json` if it exists (map name without the PIE prefix, e.g. `L_Demo01_A_FrozenLake.json`; `LevelJsonRules::ResolveLevelJsonFile`, used by `ACodexTacticsGameMode::LoadLevelConfig` and therefore by `PeekLevelConfig` / the mission intro flag); else the game mode BP's `LevelJsonFile` (default `level_01_outpost.json`, so L_MovementTest / L_PatrolTest are unchanged). A new map gets its own level by saving a JSON with the map's name (Wave Editor: level_id = map name). `L_Demo01_A_FrozenLake.json` is a DRAFT (3 sudden waves on lanes NORTH_ICE / EAST_GULLY / WEST_SHORE, `combat_start: button`, horde off, loadout mines 2 / barricades 1 / turret 1 / matches 3; the `matches` key is only in the Wave Editor type, C++ does not read it yet; `breach_wave` / `susanin_wave` keys do not exist: the breach is the map's dynamic spawn point, Susanin the SusaninSpawn point).
2. **Volumes**: Place Actors -> `TutorialTriggerVolume` (class `ATutorialTriggerVolume`, amber box; resize the `Box` component).
   Details -> **CodexTactics|Tutorial**: `Step Id` (step to start), `Beat Id` (beat to start: checkpoint + lesson rule),
   `Volume Id` (the name steps use in `"trigger": {"type": "volume", "volume": ...}` / `"complete": {"volume": ...}`; empty =
   Step Id), `Required Beat` (fires only from that beat on), `Activated By` (Only The Leader / Any Operative), `Fire Once`.
   Without steps / with the tutorial off it does nothing and is not used up.
3. **Markers**: Place Actors -> `TutorialMarkerActor` (`ATutorialMarkerActor`: ground ring + bobbing beacon, visible in the editor,
   in game only while active). `Step Id` (shown while that step is on screen) or `Beat Id` (shown during that beat); `Ring Radius Cm`,
   `Show Beacon`, `Beacon Height Cm`, `Screen Edge Pointer` (overlay arrow when off-screen), `Color` (amber), `Marker Material`
   (Gemini's material slot, needs a vector parameter `Color`; request #18).
4. **Steps**: `Content/Data/Tutorial/<file>.json` (staged as UFS). Root `{ "id", "version", "start_beat", "beats": [...], "steps": [...] }`.
   Beat: `{ "id", "lesson": bool (lethal hits only down), "checkpoint": bool (the "checkpoint" save at its start = the previous stage done; rev 5f: the only automatic save), "title_id" }`.
   Step: `{ "id", "beat", "kind": hint_card | objective | radio | walk_talk, "text_id", "keys": [command ids or literal keys],
   "trigger": { "type": manual | beat_start | volume | event | after_step | first_use | failure_retry, "volume", "event", "step",
   "command", "count", "delay_s" }, "complete": { "commands": [...], "count", "hold_s", "event", "distinct_operatives", "volume", "timeout_s" }, "once", "skippable",
   "blocking" (always false), "lines": [ { "speaker", "text_id", "duration_s" } ] }`. No trigger = `beat_start`.
   **2026-10-12 additions (Demo 01 A0-A1 redesign):** kind `feed` (its line(s) go to the message feed at once, no display slot,
   completes immediately: short reactions while a card / speech is up). Beat and step `"lock": [...]` / `"unlock": [...]` (command id
   patterns, `"*"` = all; beat: at its start; step: when shown, or when it finishes without showing; unlock first, then lock): a
   locked `Order.Sprint` walks, the try raises `Tutorial.Locked` / `Tutorial.Locked.Order.Sprint` (trigger a hint with
   `{"type": "event", "event": "Tutorial.Locked.Order.Sprint"}`); saved with the progress; tutorial OFF = no locks. Trigger type
   `condition` + `"trigger.requires": [step ids]` (the trigger counts only after those steps were triggered; works for every trigger
   type) + step `"condition": {"stat": ..., "at_least" | "below": N}` (or an array = AND; stats `squad_cold_max`, `squad_cold_min`,
   `leader_cold`, `squad_heat_packs`): for a `condition` trigger it fires the step; on any step it is re-checked when the step is due
   (after `delay_s`) and a failed check skips it silently (its `after_step` followers still fire). Step `"action": {"type":
   "cold_bump", "amount": 20, "min_level": 42}` (or `"actions": [...]`): run when the step is SHOWN (a walk-and-talk waits for the
   speech slot). Step `"focus": "coldest_operative"`: the card / feed text may use `{focus}` (his name), he gets a floating
   "COLDEST" line, `FTutorialCardView::FocusOperative / FocusName` for the WBP. Unknown stat / action / focus, a `condition`
   trigger without a condition and `requires` naming an unknown step are parse ERRORS. Events for loot: `Loot.Open` (command, crate
   window opened), `ItemLooted` / `ItemLooted.<Item>` (`HeatPack`, `Medkit`, ... - taken into an operative's kit); heat pack used:
   `ItemUsed.HEAT_PACK`. Patterns:
   exact, `"Prefix.*"`, `"*"`. **`"precredit"` (2026-10-13, default true):** a once-only hint card completed by COMMANDS whose completion
   the player's command history already satisfies when the card becomes due (uses / hold time since the steps loaded, recorded even while
   a radio plays or a card is not up) is completed at once without showing ("✓ <title> - already done." in the feed, telemetry
   `precredited`); fewer uses than needed seed the card's progress (one Q press before the Q/E card = 1 of 2). `"precredit": false` for
   lessons that must happen at that moment (regroup, the sprint card, loot open, the tripwire). Event / volume completions and `"once":
   false` cards never precredit. The history is saved in the tutorial section (`"history"`) and forgotten by RESET TUTORIAL. Lock targets
   today: every player command family (rev 5b progressive unlock, see the block at the top); `Order.Sprint` degrades to a walk,
   everything else is refused by its handler; each refusal raises `Tutorial.Locked.<Id>` once per try.
   `"distinct_operatives": true` (2026-10-12): each operative's event counts once, so `count` = how many
   different operatives must do it ("put each operative into cover": `{"event": "CoverEntered", "count": 3, "distinct_operatives": true}`;
   "enter cover 4 times": `{"event": "CoverEntered", "count": 4}`). **`hold_s` (2026-10-12):** accumulated seconds a CONTINUOUS command was
   active while the card is SHOWN (never during the card breather, never while merely queued), in addition to `count` uses:
   `{"commands": ["Camera.Pan.Keys", "Camera.Pan.Edge"], "hold_s": 1.0}`. Continuous inputs report ONE use when the action starts and
   then `PlayerCommands::ReportHold(id, dt)` ticks (`FPlayerCommandContext::HeldSeconds` > 0, same bus; ticks are never uses and never
   fire `first_use`); `FTutorialStateMachine::HandleCommandHold`, rule `TutorialRules::IsProgressMet`. A wildcard is a plain prefix match
   so it spans segments (`Camera.*`, `Camera.Pan.*`). **Camera ids (2026-10-12, were Camera.Pan / .Rotate):** `Camera.Pan.Keys` (WASD, hold ticks),
   `Camera.Pan.Edge` (cursor at the screen edge, hold ticks), `Camera.Pan.Drag` (MMB drag, hold ticks), `Camera.Rotate.Keys` (Q / E, one per
   press), `Camera.Rotate.Mouse` (RMB drag, hold ticks), `Camera.Zoom` (one per wheel notch); key hints in the registry = the F1 Controls
   screen rows (6 camera rows now). **Beat A0 is STAGED:** `A0_cam_pan` (WASD / edge, hold 1 s) -> `A0_cam_rotate_keys` (Q/E x2) ->
   `A0_cam_rotate_mouse` (RMB drag, hold 0.5 s) -> `A0_cam_zoom` (wheel x2) -> `A0_move`; no timeout (skippable); texts
   `TUT_A0_CAM_PAN / _ROTATE_KEYS / _ROTATE_MOUSE / _ZOOM` (draft file). The card counter is per beat ("n / 5" in A0, move included). Sample: `demo_01_steps.json` beat A6 (`A6_*`, volume `VOL_A6_FIELD`). A hint card without completion gets `timeout_s` 12; radio / walk-and-talk steps without completion
   end when their lines have played. Body text may contain `{key:Squad.Solo}` (-> `[B]`). Unknown keys / commands are warned in the
   log; the unit test `CodexTactics.Tutorial.Schema.DemoFile` fails on any warning of `demo_01_steps.json`.
5. **Texts** live in the narrative sheet (Tutorial tab, request #17) -> `narrative_manifest.json` `"tutorial": { "<id>":
   { "title_en", "text_en", "radio_en", "speaker_en" } }`; a walk-and-talk `text_id` may also name a `sequences` entry. Until then
   the overlay shows `[EN missing: <id>]`.
6. **Overlay look**: a WBP child of `UTutorialOverlayWidget` set as the HUD's `TutorialOverlayWidgetClass`, with widgets named
   `TutorialHintCard` (Border), `TutorialCardTitle`, `TutorialCardBody`, `TutorialCardKeys`, `TutorialCardProgress`,
   `TutorialCardDone` (TextBlocks), `TutorialSkipButton`, `TutorialTracker` (VerticalBox) + `TutorialObjective1..3`,
   `TutorialRadioStrip` (Border), `TutorialRadioSpeaker`, `TutorialRadioLine`, `TutorialRadioHint` (TextBlock, rev 5b),
   `TutorialRadioSkipButton` (Button, rev 5b; make it non-focusable), `TutorialRetryPanel`, `TutorialRetryText`,
   `TutorialRetryButton`, `TutorialContinueButton`. Keep panels Self-Hit-Test-Invisible (only the buttons take clicks).
7. **Testers' log**: `Saved/Tutorial/tutorial.jsonl` (one JSON per line: session, map, event shown / completed /
   completed_silently / skipped / timed_out / beat_started / lesson_downed / retry / enabled / disabled, step, beat, active_s, game_s).

