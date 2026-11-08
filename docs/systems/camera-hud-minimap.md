# Camera, roof cutaway, HUD, action bar and minimap

> `ATacticalCameraPawn` + `ACameraZoneVolume` (camera zones per mode), `URoofCutawaySubsystem` (roofs hide when the selected operative is inside), `InteriorExitRules`
> (exit arrows), `UMinimapSubsystem` (one capture per floor, fog, icons) and the canvas HUD (`ACodexTacticsHUD`) with its units.
>
> Key files:
> - `Camera/TacticalCameraPawn`, `CameraZoneVolume`, `RoofCutawaySubsystem`, `InteriorExitRules`, `UI/MinimapSubsystem`, `UI/CodexTacticsHUD*`, `UI/ActionBarWidget`, `PhaseBannersWidget`
> - Design `docs/design/MINIMAP.md`; smokes `MinimapSmoke`, `RoofCutawaySmoke`, `RoofCutawayFocusSmoke`, `CameraZoneSmoke`, `HudShot`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Minimap (old §5)

**Minimap (user decisions 2026-10-18, UE-only; design `docs/design/MINIMAP.md`)** — the top-right panel shows the message feed OR the
map (F6 / a click on its `MAP | MESSAGES` header; session memory `UMissionSessionSubsystem::MinimapPanelMode`, default `Codex.Minimap.DefaultPanel`
0 = messages; unread count in the header), full-screen map on Tab (not in turn-based combat: Tab = next operative there) / F7, Esc closes it.
`UMinimapSubsystem` (`UI/MinimapSubsystem.*`): at load (>= 1 s, navmesh built) area = level JSON `"minimap.bounds_cm"` or the navmesh bounds
volumes; floors = `"minimap.floors_cm"` or auto (stacked walkable surfaces on a coarse grid refined round stacked columns ->
`MinimapRules::DetectFloorBases` -> kept only with a navmesh path from the leader); per floor a hidden-component list
(`MinimapRules::HideOnFloor`: ground = bottom >= 2 m above the ground under it, floor k = >= 2 m above its surface; traces see `WorldStatic` +
`WorldDynamic` since 2026-10-21 - the lake hut's floor slab is dynamic and its roof stayed) and ONE capture (`ASceneCapture2D`, orthographic
**straight down** at `ATacticalCameraPawn::Config.BaseYaw` since 2026-10-21 - map up = the start camera's forward; `Codex.Minimap.TopDown 0` = the
old iso view at `Config.Pitch`; top-down upper floors clipped 2 m above their surface by the near plane, `MinimapRules::FloorClipZ`; sRGB RGBA8
2048, manual exposure, 4 passes; pawns / markers / traps / runtime helpers hidden); never re-captured unless `CodexTactics.MinimapRecapture` (dev). Live: every 0.25 s each operative's floor
(`MinimapRules::SelectFloor`: nothing walkable >= 1.1 m below = ground; band edges with hysteresis) + a 15 m fog disc on his floor
(`FMinimapFogGrid`, 2 m cells over the image's footprint, per floor, texture upload on change), enemy memory (seen = sight system in a fight /
clear line within 25 m exploring; last known spot fades 12 s); `BuildIcons` (squad with facing + leader frame, enemies, shown tutorial markers,
spotted mines / wires, building doors once explored; facing arrows via `FMinimapIsoProjection::MapDirection`, square image units) + `GetCameraFootprint`. Input (`CodexTacticsPlayerControllerMap.cpp`): wheel over the map =
map zoom, LMB = `ATacticalCameraPawn::LookAtPoint` (holds the spot until a pan / leader change / cinematic), RMB = `HandleWorldHit` with no actor at
the resolved spot (trace along the view ignoring the floor's hidden pieces, then the navmesh); the cursor over the map: no edge scroll, no world
hover. HUD unit `CodexTacticsHUDMinimap.cpp` (image window, fog quad clipped by `MinimapRules::ClipPolygonToRect`, footprint, icons). Save
`world_state.minimap` (panel + fog per floor, base64; another grid = ignored; a load before setup is applied once set up). Commands `Map.Panel` /
`Map.Full` / `Map.Camera` / `Map.Move` / `Map.Zoom` (F8 «Movement & camera», never tutorial-locked). Tests `CodexTactics.UI.Minimap.*`; smoke
`MinimapSmoke`. Perf: setup 4-7 ms, capture ~1 ms game thread per pass, update 20-80 us / 0.25 s, icons + canvas ~40-70 us a frame; GPU cost of
the passes -> Gemini request #24.

## Tutorial edge arrow and the highlighted target (2026-10-20)

**The current navigation target** of the tutorial (rule: [tutorial-engine](tutorial-engine.md) section 17) is the only thing the HUD points
at: `UTutorialOverlayWidget::UpdateEdgePointers` draws ONE small amber chevron (20 px, 3 px lines, `PointerColor`) at the screen edge
(48 px inset) towards it while it is off-screen, with its 2D distance from the leader ("42 m", `TutorialRules::FormatGuideDistance`, bold 11)
just inside the tip; on-screen: no arrow (the marker / guide beacon itself shows). The view is the player camera's perspective
(`TutorialRules::ProjectToView`: camera location / rotation / horizontal FOV of `APlayerCameraManager`; a point behind the camera points
the arrow to its side) in the viewport's widget units - headless (no viewport) a 1920 x 1080 view, so smokes check the same rule
(`GetGuidePointer`, `GetPointerViewSize`). Other shown tutorial markers keep their rings / labels but no arrow (before 2026-10-20 every
shown marker had one - the bridge objective's arrow kept pointing back from the hut). **Minimap:** `UMinimapSubsystem::BuildIcons` marks
the target's objective icon `bHighlight` (exactly one; added at the target's location if its marker is not in the 1 s static list yet - the
runtime guide beacon); the HUD unit draws it as a larger diamond with a white frame. Tests `CodexTactics.Tutorial.Guide.EdgePointer`;
TutorialSmoke stages 152-153 (the camera turned away: one arrow along the target's direction, "n m"; the minimap highlights one icon).

## Camera, roof cutaway, building exits (old §5)

**Camera** — `Camera/TacticalCameraPawn` (+Rules), `CameraZoneVolume`. Godot `camera.gd`, `camera_zone_trigger.gd`.
**Camera detach (2026-10-26)** — a manual pan (WASD, edge scroll, MMB drag, minimap look-at) in the free view (exploration with `Codex.Camera.FreeExploration` 1, or real-time combat) freezes the camera focus: a walking leader / a move order does not drag the view. Only `ATacticalCameraPawn::ReattachToFollowTarget` brings it back: the squad number keys 1-4 and the portrait click (`ACodexTacticsPlayerController::SelectMember`, the leader's own number too), or a leader change; glides, snaps, cinematic lock and `ExitTurnBasedZoom` also clear it. Turn-based combat is never detached (its camera follows the active unit / enemy). Smoke `CameraStaySmoke`.
**Free pan without a leash (the game designer's play-test 2026-11-01, bug_04 B)** - "after panning far enough along X or Y the camera stops,
tied to the selected character; only a click on him gets it going again". Root cause: every pan clamped its offset to `MaxPanCombat` (85 m)
round the camera focus (`TacticalCameraRules::ClampPan`), and a manual pan freezes the focus at the selected operative (camera detach) - a radius
leash round him. Now the free pan view (exploration with `Codex.Camera.FreeExploration`, every fight view) limits only the view's centre to the
level's area (`UMinimapSubsystem::GetLevelArea`, the minimap's) grown by `FreePanLevelMarginCm` (30 m), by axis, never relative to a character
(`ATacticalCameraPawn::LimitPan` -> pure `TacticalCameraRules::ClampFreePan`, test `CodexTactics.Camera.FreePanNoLeash`; no area known yet: no
limit); the old exploration leash (`FreeExploration` 0) keeps `ClampPan`. A/B `Codex.Camera.FreePanBounds 0` = the old leash. Smoke
`CameraFreePanSmoke` (L_FormationLab: 70 m along X then 70 m along Y from the leader = 99 m, still moving over each leg's last 10 m; past the
level's edge it stops at area + 30 m; the number key re-centres).

**Roof cutaway (rev 5h item 3, game designer 2026-10-15, every level, UE-only)** — `Camera/URoofCutawaySubsystem` (tickable world subsystem, Game / PIE; CVar `Codex.RoofCutaway` 1 / 0) + pure `RoofCutawayRules` (tests `CodexTactics.Camera.RoofCutaway.*`). Every `FRoofCutawayConfig::UpdateIntervalSeconds` 0.1 s of real time, for the selected operative if living, visible and not mounted (since 2026-11-02, "Whose roof" below; before: for every such squad operative): skipped when he stands in an `ACameraZoneVolume` whose fixed camera `IsZoneCameraShown` (the M outside view hides it -> cut as usual; `ShouldCutForOperative`); an object-type (WorldStatic / WorldDynamic) line trace straight up from his head (capsule top, at least feet + 180 cm) + 10 cm to feet + 15 m steps over pieces that may not be cut (up to 8) and takes the first that may (`ShouldCutPiece`: a visible `UStaticMeshComponent` - ISM / HISM included - of no pawn, no landscape, no foliage (by class name: the Landscape / Foliage modules are no dependencies), not tagged `NoRoofCutaway`, its bounds' bottom >= head + `HeadClearanceCm` 40, or the actor tagged `RoofCutaway`) = the roof actor; every piece of that actor passing the rule is cut, their bounds = the footprint (a footprint wider than `MaxFootprintCm` 60 m on an axis = no building: nothing cut). Graybox buildings of separate actors: a box overlap (object types) over the footprint (inset `FootprintInsetCm` 10) from head + 40 cm to the top adds every piece of other actors that passes the rule and overlaps the footprint by > 10 cm on both axes (ceilings, upper floors; walls standing on the floor never); `RoofCutaway`-tagged actors (cached once a second, bounds incl. non-colliding pieces) overlapping the footprint or hanging over his head are cut whole. **Cut** = `SetRenderInMainPass(false)` + `SetRenderInDepthPass(false)` (still visible: shadows, Lumen, distance fields keep it - the interior is not sunlit) + Visibility response Ignore (cursor clicks reach the floor, no silhouette through it; object queries still find it, so the roof stays "found" while cut); the piece's own flags / response are restored **`RestoreDelaySeconds` 0.5 s** after no operative was under it (`ShouldRestore`), at `Deinitialize`, or with the CVar off. Hook for a dithered fade: `OnCutawayChanged(Piece, bCutAway)` (native multicast) - Gemini request #22 in TANDEM. Smoke `RoofCutawaySmoke` (+ `Shot` -> `Saved/Screenshots/roof_cutaway.png`). **Whose roof (2026-11-02, the game designer's bug report on `L_Gen_Depot_1`, item 8: "2 men on a roof, 1 on the ground - the roof was see-through; it must follow the operative I work with"):** only the SELECTED operative's roof is cut - the squad leader (the selected man; the leader of a box-selected group; the active unit in turn-based combat, which `SetLeader`s it) - `RoofCutawayRules::ShouldEvaluateOperative`, CVar `Codex.RoofCutaway.FollowSelected` (1; 0 = every operative's roof, the old rule). The selected man on the roof keeps it drawn while a team-mate stands under it; selecting the man under it cuts the roof above him; back to the roof man -> restored after the 0.5 s hysteresis. The minimap already showed the leader's floor (`UMinimapSubsystem::UpdateTick` -> `DisplayedFloor`) - unchanged, now checked too. Smoke `RoofCutawayFocusSmoke` (`L_Gen_Depot_1`, B_hall_n's 9 m roof, Solo: roof man / ground man / roof man / group led from the ground / group led from the roof; the roof piece above the ground man and the minimap floor): before (`Codex.RoofCutaway.FollowSelected 0`) FAIL (the roof cut with the roof man selected), after PASS; test `CodexTactics.Camera.RoofCutaway.FollowSelected`; pictures `RampRoofShots` -> `Saved/Screenshots/RampRoof/cutaway_roofman.png`, `cutaway_groundman.png`.

**Occluder fade - REMOVED (the game designer's decision 2026-11-01, bug_05: "a failed solution" - walls never fade).** Merged the same day (ff3a0400, `formation/wall-far-side` fix c: `Camera/UOccluderFadeSubsystem` + `OccluderFadeRules`, `M_OccluderFade`, CVar `Codex.OccluderFade`, the cursor's `ThroughFadedOccluders` step in `HandleWorldHit`, the targets from `UpdateWallSideHover`) and taken out on branch `fix/custom-ghosts-solo`: the subsystem, its tests, the material and its script are deleted, the cursor's hit is the plain visibility hit again. The rest of the far-side work stays (squad-and-orders.md "The far side of a wall": the wall side by the cursor, the tight pick, the column route); a wall that hides the ground right behind it simply hides it - the top's far half is the click for the far face. The roof cutaway above is unchanged.

**Building exits (rev 5g item 5, user request 2026-10-14, UE-only)** — pure `Camera/InteriorExitRules` (`BuildEdgeSamples`, `FindDoorRuns` (>= `MinDoorWidthCm` 60), `GetExitOrderScope` (Solo = selected, else squad), `ShouldShowZoneCamera` (+ the player override), `ShouldShowExitArrows` (not in turn-based); tests `CodexTactics.Camera.InteriorExit.*`), `Camera/UInteriorExitSubsystem` (tickable: while a zone `IsZoneCameraShown` spawns one `AInteriorExitArrowActor` per doorway - placed `ABuildingExitMarker`s of that zone (`Zone` set, or inside its box + 2 m) win, else the automatic search: every box edge sampled every 20 cm, a point 120 cm inside and 100 cm outside both on the infantry navmesh (<= 35 cm off) and joined by `NavigationRaycast` = crossable, runs = doors, exit target 2 m out projected on the navmesh; `OrderExit` = `ComputeGroupTargets` round the exit target, `OrderMoveTo` (own orders), feed line; `ToggleInteriorView`), `ACameraZoneVolume::TogglePlayerOutsideView` (reset in `Deactivate`; `bWantCamera` via `ShouldShowZoneCamera`), `GetZoneTransform` / `GetZoneHalfExtent`; `Deactivate` keeps the followers holding in Solo. Controller: a click on an arrow (`HandleWorldHit`, before the turn-based branch) -> `OrderExit` + command `Order.ExitBuilding`; hover in the trap-hover block (hand cursor, `SetHoveredArrow`); **M** (`BindKey`, free key) -> `InteriorViewKey` + `Camera.InteriorView`; both never locked (`Tutorial.Lock.ProgressiveDemo` allow-list). HUD `DrawInteriorViewHint` bottom right. The arrow look (engine cube + cone in `BasicShapeMaterial` amber, opaque text) is a baseline - restyle freely. Smoke `InteriorExitSmoke` (review map: all three inside -> interior camera + 1 arrow at the north door; click -> all out, zone off; M -> outside view, M -> back; Solo -> only the selected one leaves); `InteriorExitSmoke Shot` (rendered) saves `Saved/Screenshots/interior_exit_arrows.png`.

## Phase banners and action bar (old §5)

2026-11-01 (bug 08): no combat banner during a fight and no enemy count anywhere on screen - see waves-horde-encounters.md "No enemy count on screen".

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

