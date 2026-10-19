# Camera, roof cutaway, HUD, action bar and minimap

> `ATacticalCameraPawn` + `ACameraZoneVolume` (camera zones per mode), `URoofCutawaySubsystem` (roofs hide when the squad is inside), `InteriorExitRules`
> (exit arrows), `UMinimapSubsystem` (one capture per floor, fog, icons) and the canvas HUD (`ACodexTacticsHUD`) with its units.
>
> Key files:
> - `Camera/TacticalCameraPawn`, `CameraZoneVolume`, `RoofCutawaySubsystem`, `InteriorExitRules`, `UI/MinimapSubsystem`, `UI/CodexTacticsHUD*`, `UI/ActionBarWidget`, `PhaseBannersWidget`
> - Design `docs/design/MINIMAP.md`; smokes `MinimapSmoke`, `RoofCutawaySmoke`, `CameraZoneSmoke`, `HudShot`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Minimap (old §5)

**Minimap (user decisions 2026-10-18, UE-only; design `docs/design/MINIMAP.md`)** — the top-right panel shows the message feed OR the
map (F2 / a click on its `MAP | MESSAGES` header; session memory `UMissionSessionSubsystem::MinimapPanelMode`, default `Codex.Minimap.DefaultPanel`
0 = messages; unread count in the header), full-screen map on Tab (not in turn-based combat: Tab = next operative there) / F3, Esc closes it.
`UMinimapSubsystem` (`UI/MinimapSubsystem.*`): at load (>= 1 s, navmesh built) area = level JSON `"minimap.bounds_cm"` or the navmesh bounds
volumes; floors = `"minimap.floors_cm"` or auto (stacked walkable surfaces on a coarse grid refined round stacked columns ->
`MinimapRules::DetectFloorBases` -> kept only with a navmesh path from the leader); per floor a hidden-component list
(`MinimapRules::HideOnFloor`: ground = bottom >= 2 m above the ground under it, floor k = >= 2 m above its surface) and ONE capture
(`ASceneCapture2D`, orthographic at `ATacticalCameraPawn::Config.BaseYaw / Pitch`, sRGB RGBA8 2048, manual exposure, 4 passes; pawns / markers /
traps / runtime helpers hidden); never re-captured unless `CodexTactics.MinimapRecapture` (dev). Live: every 0.25 s each operative's floor
(`MinimapRules::SelectFloor`: nothing walkable >= 1.1 m below = ground; band edges with hysteresis) + a 15 m fog disc on his floor
(`FMinimapFogGrid`, 2 m cells over the image's footprint, per floor, texture upload on change), enemy memory (seen = sight system in a fight /
clear line within 25 m exploring; last known spot fades 12 s); `BuildIcons` (squad with facing + leader frame, enemies, shown tutorial markers,
spotted mines / wires, building doors once explored) + `GetCameraFootprint`. Input (`CodexTacticsPlayerControllerMap.cpp`): wheel over the map =
map zoom, LMB = `ATacticalCameraPawn::LookAtPoint` (holds the spot until a pan / leader change / cinematic), RMB = `HandleWorldHit` with no actor at
the resolved spot (trace along the view ignoring the floor's hidden pieces, then the navmesh); the cursor over the map: no edge scroll, no world
hover. HUD unit `CodexTacticsHUDMinimap.cpp` (image window, fog quad clipped by `MinimapRules::ClipPolygonToRect`, footprint, icons). Save
`world_state.minimap` (panel + fog per floor, base64; another grid = ignored; a load before setup is applied once set up). Commands `Map.Panel` /
`Map.Full` / `Map.Camera` / `Map.Move` / `Map.Zoom` (F1 «Movement & camera», never tutorial-locked). Tests `CodexTactics.UI.Minimap.*`; smoke
`MinimapSmoke`. Perf: setup 4-7 ms, capture ~1 ms game thread per pass, update 20-80 us / 0.25 s, icons + canvas ~40-70 us a frame; GPU cost of
the passes -> Gemini request #24.

## Camera, roof cutaway, building exits (old §5)

**Camera** — `Camera/TacticalCameraPawn` (+Rules), `CameraZoneVolume`. Godot `camera.gd`, `camera_zone_trigger.gd`.
**Roof cutaway (rev 5h item 3, game designer 2026-10-15, every level, UE-only)** — `Camera/URoofCutawaySubsystem` (tickable world subsystem, Game / PIE; CVar `Codex.RoofCutaway` 1 / 0) + pure `RoofCutawayRules` (tests `CodexTactics.Camera.RoofCutaway.*`). Every `FRoofCutawayConfig::UpdateIntervalSeconds` 0.1 s of real time, for every living, visible, not mounted squad operative: skipped when he stands in an `ACameraZoneVolume` whose fixed camera `IsZoneCameraShown` (the M outside view hides it -> cut as usual; `ShouldCutForOperative`); an object-type (WorldStatic / WorldDynamic) line trace straight up from his head (capsule top, at least feet + 180 cm) + 10 cm to feet + 15 m steps over pieces that may not be cut (up to 8) and takes the first that may (`ShouldCutPiece`: a visible `UStaticMeshComponent` - ISM / HISM included - of no pawn, no landscape, no foliage (by class name: the Landscape / Foliage modules are no dependencies), not tagged `NoRoofCutaway`, its bounds' bottom >= head + `HeadClearanceCm` 40, or the actor tagged `RoofCutaway`) = the roof actor; every piece of that actor passing the rule is cut, their bounds = the footprint (a footprint wider than `MaxFootprintCm` 60 m on an axis = no building: nothing cut). Graybox buildings of separate actors: a box overlap (object types) over the footprint (inset `FootprintInsetCm` 10) from head + 40 cm to the top adds every piece of other actors that passes the rule and overlaps the footprint by > 10 cm on both axes (ceilings, upper floors; walls standing on the floor never); `RoofCutaway`-tagged actors (cached once a second, bounds incl. non-colliding pieces) overlapping the footprint or hanging over his head are cut whole. **Cut** = `SetRenderInMainPass(false)` + `SetRenderInDepthPass(false)` (still visible: shadows, Lumen, distance fields keep it - the interior is not sunlit) + Visibility response Ignore (cursor clicks reach the floor, no silhouette through it; object queries still find it, so the roof stays "found" while cut); the piece's own flags / response are restored **`RestoreDelaySeconds` 0.5 s** after no operative was under it (`ShouldRestore`), at `Deinitialize`, or with the CVar off. Hook for a dithered fade: `OnCutawayChanged(Piece, bCutAway)` (native multicast) - Gemini request #22 in TANDEM. Smoke `RoofCutawaySmoke` (+ `Shot` -> `Saved/Screenshots/roof_cutaway.png`).

**Building exits (rev 5g item 5, user request 2026-10-14, UE-only)** — pure `Camera/InteriorExitRules` (`BuildEdgeSamples`, `FindDoorRuns` (>= `MinDoorWidthCm` 60), `GetExitOrderScope` (Solo = selected, else squad), `ShouldShowZoneCamera` (+ the player override), `ShouldShowExitArrows` (not in turn-based); tests `CodexTactics.Camera.InteriorExit.*`), `Camera/UInteriorExitSubsystem` (tickable: while a zone `IsZoneCameraShown` spawns one `AInteriorExitArrowActor` per doorway - placed `ABuildingExitMarker`s of that zone (`Zone` set, or inside its box + 2 m) win, else the automatic search: every box edge sampled every 20 cm, a point 120 cm inside and 100 cm outside both on the infantry navmesh (<= 35 cm off) and joined by `NavigationRaycast` = crossable, runs = doors, exit target 2 m out projected on the navmesh; `OrderExit` = `ComputeGroupTargets` round the exit target, `OrderMoveTo` (own orders), feed line; `ToggleInteriorView`), `ACameraZoneVolume::TogglePlayerOutsideView` (reset in `Deactivate`; `bWantCamera` via `ShouldShowZoneCamera`), `GetZoneTransform` / `GetZoneHalfExtent`; `Deactivate` keeps the followers holding in Solo. Controller: a click on an arrow (`HandleWorldHit`, before the turn-based branch) -> `OrderExit` + command `Order.ExitBuilding`; hover in the trap-hover block (hand cursor, `SetHoveredArrow`); **M** (`BindKey`, free key) -> `InteriorViewKey` + `Camera.InteriorView`; both never locked (`Tutorial.Lock.ProgressiveDemo` allow-list). HUD `DrawInteriorViewHint` bottom right. The arrow look (engine cube + cone in `BasicShapeMaterial` amber, opaque text) is a baseline - restyle freely. Smoke `InteriorExitSmoke` (review map: all three inside -> interior camera + 1 arrow at the north door; click -> all out, zone off; M -> outside view, M -> back; Solo -> only the selected one leaves); `InteriorExitSmoke Shot` (rendered) saves `Saved/Screenshots/interior_exit_arrows.png`.

## Phase banners and action bar (old §5)

**Phase banners** — `UI/PhaseBannersWidget`: pause banner «РЕЖИМ ПРИКАЗОВ | Зарядов… | Время планирования…» (the free
pause of a sudden wave: «CONTACT - free pause, charges N/M kept» + the flyover caption «HOSTILE: <type> | [Esc / click] skip»),
combat banner «ПОДГОТОВКА К БОЮ: NN сек» + «Начать бой» (`FinishPreparation`) / «ВОЛНА N | ВРАГОВ ОСТАЛОСЬ: M»,
pre-combat cutscene card (black screen, countdown; click or Space skips = `FinishCutscene`). The mission subsystem
resets the squad (cold 0, full health, stop) when the cutscene ends (Godot `_end_cutscene_and_start_pause`).

**Action bar** — `UI/ActionBarWidget` (Godot TacticalBar, bottom centre): «ИНВ» (inventory drawer; Sprint 13 retired
«ПЕРЕД» — hand-overs are dragged from the drawer onto a model or a squad slot), weapon slot «M16 [clip/reserve] | [G] Граната» («Перезарядка...»), «ПЕР» (object-pick mode:
`ACodexTacticsPlayerController::ToggleRelocateSelectMode`, next object click → `StartRelocate`; «АКТИВ» while
picking / placing), stance letter С / П / Л (click → `CycleLeaderStance`), «ОБОР» (guard — disabled until ported),
squad slots [1] КОМ [2] ИНЖ [3] МЕД + HP / cold bars, [4] РЕЗ locked. Hidden while the start menu is open. Godot
`main.gd _update_tactical_command_bar`, `_cycle_leader_stance`, `_on_relocate_slot_clicked`.

## UI / HUD (old §5)

**UI** — `UI/CodexTacticsHUD` (canvas: objective banner «ЦЕЛЬ: …» top left, message feed, squad panel with supply,
labels; owns the action menu, loot dialog and `UI/MissionFailedWidget` «МИССИЯ ПРОВАЛЕНА»). Godot colours go
through `ACodexTacticsHUD::GodotColor` (sRGB → linear) in UMG and canvas.
`UI/GameMessageSubsystem` (feed, logs every line to `LogCodexTactics`).
Text caches (cleanup 2026-10-16, audit P6 / P7, `UI/HudTextCache`): world labels are stripped / split / measured once per label
text (`LabelLayouts`), feed messages wrapped once per text and feed width (`FeedWraps`, `FeedSpeakers`), floating texts
stripped once (`StrippedFloatingTexts`); all keyed case-sensitively on the raw text, bounded (dropped when full), dropped on a
font / width change (`ValidateTextCaches`). `UActionBarWidget::Refresh` (every frame) only calls a setter when the value changed
(`ActionBarDirty::*`, `SetToolTipIfChanged`: `SetToolTipText` built a new SToolTip each call) - audit P8.

