# Dialogue, frontend (menus), mission and narrative pipeline

> Dialogues are DataAssets imported from Godot (`import_dialogues.py`); the frontend is a CommonUI layer stack (`UCodexPrimaryLayout`, `UCodexUISubsystem`)
> where the artist edits widgets by name; `UMissionSubsystem` owns objectives, mission failed and restart. All in-game text is English, texts live in the
> narrative sheet (`Scripts/Narrative/sync_narrative.py`).
>
> Key files:
> - `UI/DialogueSubsystem`, `Data/DialogueSequenceAsset`, `UI/Frontend/*`, `Core/MissionSubsystem`, `MissionRules`, `MissionSessionSubsystem`
> - Assets `/Game/UI/Frontend/...`; smokes `FrontendSmoke`, `MainMenuSmoke`, `DialogueSmoke`, `MissionSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Dialogues, frontend, mission start, mission (old §5)

**Dialogues** — `Data/DialogueSequenceAsset` (lines: speaker, text, delay) imported from the Godot .tres by
`Scripts/Editor/import_dialogues.py` into `/Game/Data/Dialogues/DA_*` (intro, prep, wave rest, victory, Susanin
recruitment) — re-run it when Godot texts change, never hand-edit. `UI/DialogueSubsystem`: `StartDialogue` = bottom
window `UI/DialogueWidget` (speaker card, [N / M], «Пропустить» / «Далее»; Space / Enter / click next, Esc skip, world
orders blocked), `PlayInFeed` = timed lines in the message feed. `UI/DialogueRules` (speaker card, button texts).
Game mode soft refs `DialogueMissionStart / PreparationStarted / WaveRest / Victory`. Mission hooks: start (Game /
Exploration: intro window; headless starts get the radio line instead), first preparation (prep), later preparations
(wave rest), PostCombat (victory, then objective «РУБЕЖ ЗАЧИЩЕН: Исследуйте…» + HQ line). Portraits: Godot emoji have
no UE font glyphs → role tags КОМ / ИНЖ / МЕД / ЖИТ / ?. Godot `bottom_dialogue_dialog.gd`, `main.gd play_dialogue`.

**Frontend (2026-10-08 user decision; architecture modelled on a CommonUI frontend, own implementation)** —
`Source/CodexTactics/*/UI/Frontend/`, plugin **CommonUI** (+ CommonInput; `CommonGameViewportClient` in DefaultEngine.ini,
input data / controller data in DefaultGame.ini, `bEnableDefaultInputConfig=False` so screens never switch the game's
input mode). Pieces: `UCodexPrimaryLayout` (one `UCommonActivatableWidgetStack` per layer tag `Codex.UI.Layer.Game /
GameMenu / Menu / Modal`, z-order 100 above the HUD, never hit-testable itself); `UCodexUISubsystem` (GameInstance:
`GetOrCreateLayout`, `PushScreen(LayerTag, ScreenTag, Init, OnPushed)` — soft classes from the settings, async-loaded
(all preloaded when a layout is created), C++ fallback class when a WBP is missing; `ShowConfirm`, `FindScreen`,
`GetTopScreen`, `ClearLayer`, `HandleBackOnTopScreen`); `UCodexFrontendSettings` (Project Settings > Game > Codex
Frontend: screen tag -> WBP, frontend / new-game level, camera blend time / function / exponent, title anchor id);
`UCodexActivatableScreen` (base: placeholder tree via `ICodexDefaultTree::BuildDefaultTree`, widgets found by name,
`UCodexMenuButton` entries — hover / focus = select (description line), click = activate, entry `Back` = back action);
`UCodexMenuButton` (CommonButtonBase: label, description, EntryId, CameraAnchorId); screens `UCodexTitleScreen`
(any key), `UCodexMainMenuScreen`, `UCodexSaveSlotsScreen` (+ `UCodexSaveSlotEntry`; save / load / delete, overwrite
confirm), `UCodexOptionsScreen` (placeholder), `UCodexCreditsScreen`, `UCodexPauseMenuScreen`, `UCodexConfirmDialog`
(Yes / No or OK, blur + dim); `UCodexFrontendSubsystem` (NEW GAME / CONTINUE / LOAD / QUIT TO MAIN MENU / QUIT GAME);
`CodexSaveBridge` (the only place touching `USaveGameSubsystem`: `CanSaveNow`, `LoadGameWithTravel`, `GetContinueSlot`,
slot list); menu map actors `ACodexMenuCameraAnchor` (CameraActor + AnchorId + BlendTimeOverride),
`ACodexMenuCharacterActor` (skeletal mesh idling), `ACodexFrontendGameMode` / `ACodexFrontendPlayerController`
(title or main menu, `FocusCameraAnchor` = `SetViewTargetWithBlend`). Pure rules `CodexFrontendRules`. Assets:
`Scripts/Editor/create_frontend_assets.py` + editor helper `UFrontendWidgetGenerator`. Retired: `UMainMenuWidget`
(«Start game / Start battle»), `UPauseMenuWidget`, `USaveLoadDialogWidget`, the in-level start menu and `-ForceMainMenu`.

**Mission start** — `Core/MissionSessionSubsystem` (GameInstance: last mode, quick-restart flag, `bFrontendStart`,
the save system's `PendingLoadSlot`). A mission level (game mode `ACodexTacticsGameMode`) starts at once:
`UMissionSubsystem::StartMission(Game)` (Ctrl + X: the last mode again; `StartMission(Combat)` stays as the dev / bot
shortcut: quest chain completed via `UQuestSubsystem::CompleteChainForCombat`, squad healed / warmed at the actor tagged
`CombatStart`, pre-combat cutscene). The intro briefing plays unless a save is pending or the run is headless
(`-ExecCmds` / `-NoMainMenu` / `-CodexBot`) without a frontend start (`MissionRules::ShouldSkipIntro`). The frontend map
has no mission (`IsMissionWorld`). Godot `main.gd _ready`, `_on_start_game_pressed`, `_on_start_combat_pressed`.

**Mission** — `Core/MissionSubsystem` + `Core/MissionRules`: objective text (start «Исследовать КПП…», quest chain
objectives, preparation / wave «ОБОРОНА: Отразить волну N! Врагов: M» / victory texts), mission failed on any
operative death (reason hypothermia at cold ≥ 99 else wounds, HQ radio line, GameOver = time stop), `RestartMission`
(reload level; «Начать заново» and Ctrl + X). Godot `main.gd update_objective`, `_trigger_game_over`,
`_restart_current_test_mode`.
**Mission complete (2026-10-22, UE-only):** `UMissionSubsystem::CompleteMission` (once; HQ "All operatives are out...", the player's
input off, `OnMissionComplete`, `IsMissionComplete`; a later death no longer fails the mission) - called by `ULevelExitSubsystem` when every
living operative has left through the fully opened gate (level JSON `"gate_exit"`), which then fades the screen to black with "MISSION COMPLETE"
and opens the main menu 6 s later (not in headless checks). Details: [demo01-lake](demo01-lake.md) block 2026-10-22 c.
**Campaign (2026-10-22 d, UE-only):** `Content/Data/Campaign/campaign.json` orders the levels ("0.0 Frozen Dam" `L_Demo01_A_FrozenLake` -> "0.1 Frozen
Factory" `L_Demo01_B_FrozenFactory`; `UCampaignSubsystem`, `CampaignRules`). With a next level the black exit screen leads (3 s, `gate_exit.next_level_seconds`)
to the stat-points screen (`UPointsReviewWidget`: live portrait `AOperativePortraitCapture` + `UProfileDialogWidget` in review mode, "Continue >") when
somebody alive has unspent points, else straight to the next level; the squad is carried (`CampaignCarry`: progression + kit, full health, the fallen stay
behind); the next level starts behind a black screen with its title that fades in (level JSON `start_fade_seconds`, 1.5 s on any campaign arrival). Titles
also on the save slots' level column. NEW GAME still opens `NewGameLevel`. Details: [demo01-lake](demo01-lake.md) block 2026-10-22 d.

## Narrative pipeline (old §6)

- **Narrative pipeline (2026-10-08; all in-game text is English, Russian returns with a later localization pass).**
  Writer workflow in 3 steps: (1) fill the Google Sheet (columns `SequenceID, LineIndex, Speaker_EN, Text_EN, DelayAfter, ...`;
  sequence ids = `DA_Dialogue*` asset names); (2) `python Scripts/Narrative/sync_narrative.py` (writes
  `Content/Data/Narrative/narrative_manifest.json`; `--local` rebuilds from `dialogues.csv`/`glossary.csv`); (3) restart the game / PIE.
  No re-import and no editor needed: `FNarrativeManifest` (`Data/NarrativeManifest.*`) reads the JSON at runtime (staged via
  `DirectoriesToAlwaysStageAsUFS`), `UDialogueSubsystem::ResolveSequence` swaps the lines of a `DA_Dialogue*` asset for the manifest
  lines (speaker_en / text_en / delay; the asset only supplies the recruitment flag). A missing sequence / empty `text_en` shows
  `[EN missing: <seq>#<n>]` and logs a warning; Cyrillic asset text is never shown. Level-embedded notes (`ANarrativeElementActor`)
  read sequence `<ActorName>` the same way. Open list: `docs/port/narrative_missing_en.md`. Glossary names:
  `FNarrativeManifest::GetGlossaryName(key)`. Tests: `CodexTactics.Dialogue.Manifest.*`; smokes `DialogueSmoke`, `NarrativeSmoke`.
  `Scripts/Editor/apply_english_display_names.py` (headless) copies English names from LevelJson / weapons_tuning.json onto the
  `DA_Level_*` / `DA_Weapon_*` assets (LevelJson + weapons_tuning.json were translated; DA assets still hold Russian names until it is run
  with the editor closed).

## Frontend: what the artist edits (old §6)

### Frontend: what the artist edits (2026-10-08)

Everything visual is an asset; the C++ only finds widgets **by name** (keep the names, restyle / move / animate freely;
`CodexTactics.UI.Frontend.DefaultTreeWidgetNames` lists them). Swap any screen for another WBP in Project Settings >
Game > Codex Frontend (`ScreenClasses`). The placeholders were generated by `Scripts/Editor/create_frontend_assets.py`
(re-running keeps existing assets; `CODEX_FRONTEND_REBUILD=1` rewrites the WBP trees — it would undo the artist's layout).

| Visual | Asset (`/Game/UI/Frontend/...`) | Names the code uses |
|---|---|---|
| Layer root (fades between screens: the stacks' Transition settings) | `WBP_PrimaryLayout` | `GameLayer`, `GameMenuLayer`, `MenuLayer`, `ModalLayer` |
| Every menu entry button (look per state: `Styles/BP_FrontendButtonStyle`, texts `Styles/BP_FrontendText_Normal / _Hovered / _Disabled`) | `WBP_MenuButton` | `ButtonLabel` (a CommonTextBlock gets the style's text style) |
| Title «PRESS ANY KEY» | `WBP_TitleScreen` | `TitleText`, `PressAnyKeyText` |
| Main menu (list left, description bottom-left, Back hint bottom-right) | `WBP_MainMenu` | `ContinueButton` … `QuitButton` (EntryId Continue / NewGame / LoadGame / Options / Credits / Quit; label, description and CameraAnchorId editable per button), `DescriptionText`, `BackHintText`, `GameTitleText` |
| Save / load list | `WBP_SaveSlots`, line `WBP_SaveSlotEntry` | `TitleText`, `SlotList`, `EmptyText`, `SlotNameBox`, `SaveButton`, `DeleteButton`, `BackButton`, `StatusText`, `DescriptionText`; entry: `SlotTitleText`, `SlotDateText`, `SlotLevelText`, `SlotPlayTimeText` (+ BP event `OnSlotSet` for thumbnails) |
| Options (placeholder) / Credits | `WBP_Options`, `WBP_Credits` | `TitleText`, `BodyText` / `CreditsText` (lines: `CreditLines` in the class defaults), `BackButton` |
| Pause menu | `WBP_PauseMenu` | `ResumeButton`, `SaveButton`, `LoadButton`, `OptionsButton`, `ControlsButton` / `TutorialButton` / `ResetTutorialButton` (optional: an older WBP without them gets the buttons inserted after `OptionsButton` at run time), `QuitToMenuButton`, `QuitGameButton`, `DescriptionText`, `StatusText` |
| Controls reference (F1 / pause menu CONTROLS) | `WBP_Controls` (`UCodexControlsScreen`; tag `Codex.UI.Screen.Controls`; C++ fallback until the WBP exists) | `TitleText`, `ControlsList` (a ScrollBox: section headers + key / description rows are added by the code from the `PlayerCommands` registry), `BackButton` |
| Confirm dialog (blur + dim) | `WBP_ConfirmDialog` | `BackgroundBlur`, `DimBorder`, `TitleText`, `MessageText`, `YesButton`, `NoButton`, `OkButton` |
| Key icons (keyboard / gamepad) | `Input/BP_ControllerData_KeyboardMouse`, `Input/BP_ControllerData_Gamepad` (InputBrushDataMap) | — |
| Confirm / Back keys | `Input/DT_CodexUIActions` (rows Confirm, Back), `Input/BP_CodexUIInputData` | — |
| Menu scene: floor, sky, light, the idling character, cameras | `/Game/Maps/L_MainMenu` — `MenuCharacter` (`ACodexMenuCharacterActor`: mesh, `IdleAnimation` or the mesh's Anim BP), `Anchor_<Id>` (`ACodexMenuCameraAnchor`: pilot it, FOV / post process on its camera, `AnchorId`, `BlendTimeOverride`) | anchor ids = the buttons' CameraAnchorId (Title, Continue, NewGame, LoadGame, Options, Credits, Quit) |
| Blend time / curve between anchors, levels | Project Settings > Game > Codex Frontend | `CameraBlendTime` 1.2 s, `CameraBlendFunction` EaseInOut, `CameraBlendExp`, `FrontendLevel`, `NewGameLevel` |

Placeholders in phase 1: OPTIONS (text only; the tabbed options screen is a later phase), play time on save slots
("--:--" until the save metadata records it), the plain colour button style, the generated map layout.

