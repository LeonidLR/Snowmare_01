# Minimap

Status: implemented 2026-10-18 (branch `agents/work`). UE-only feature; the Godot archive has no reference.
Source of the rules: the user's decisions of 2026-10-18 in `production/session-state/active.md` («Queued 2026-10-18»).

## 1. What the player sees and does

| Action | Key / mouse | Effect |
|---|---|---|
| Switch the top-right panel | `F2`, or a click on the panel header `MAP` / `MESSAGES` | The panel shows the minimap or the message feed. In map mode the header shows `MESSAGES (n)` for feed lines not read yet. The choice is kept for the session (`UMissionSessionSubsystem::MinimapPanelMode`) and in the save. |
| Full-screen map | `Tab` (outside turn-based combat), `F3` (any mode) | Opens / closes the map over 88 % x 70 % of the screen, fitted to the whole level. `Esc` closes it too. In turn-based combat `Tab` keeps its old meaning (next operative), so `F3` is the key there. |
| Zoom | Mouse wheel over the map | Zooms the map (the camera's zoom is untouched). On the corner map the zoom is centred on the selected operative; on the full map it zooms round the cursor. |
| Camera there | LMB on the map | The main camera glides to that spot and stays there. It goes back to following the leader at the next WASD / edge / drag pan, a leader change or a scripted camera move. |
| Move there | RMB on the map | The selection walks there. The order goes through the normal ground-click path (`HandleWorldHit` with no actor at the spot), so every rule applies: tutorial locks, confinement, the combat dispatch (planned in the tactical pause), the turn-based grid, double RMB = sprint. |

- The corner map is a **rectangle in the top-right corner** (26 % of the screen width, 240-560 px, aspect 0.72), in the place of the message feed.
- It is an **isometric** view at the **level's starting camera angle** (`ATacticalCameraPawn::Config.BaseYaw` / `Pitch`, 45 deg / -35.26 deg). It never rotates when the player rotates the camera.
- It shows the **floor of the selected operative**. With several floors the corner map writes `GROUND` / `FLOOR 2` / `FLOOR 3`, and the full map's title shows the same.
- **Fog of war**: only explored areas are shown. Unexplored areas are dark: about 8 % of the level's silhouette shows through.
- The cursor over the map never scrolls the camera at the screen edge, and nothing in the world is hovered through the map.

### Icons (drawn live over the captured image, projected with the capture's own matrix)

| Icon | Shown |
|---|---|
| Squad operative | A square in his role colour (`BodyColor`) with a white facing arrow. The selected operative (the leader) is bigger and has a white frame. Box-selected members have the frame too. Operatives on another floor are dimmed. |
| Enemy | A red diamond while the squad sees it: the sight system's `IsVisibleToSquad` in a fight and, since 2026-10-19 (symmetric sight), while exploring too (eye height by stance; `Codex.Sight.Exploration 0` falls back to the old exploring rule: a clear line within 25 m). After it is lost, the **last known spot** fades from 65 % to 0 over 12 s. Enemies never seen are not shown. |
| Objective | An amber diamond for every shown tutorial marker / beacon (`ATutorialMarkerActor::IsMarkerShown`), in the marker's colour. Off the map edge it sticks to the edge. |
| Spotted trap | An orange `X` for every known live mine / PMC tripwire (`UsesDisarmApproach`: revealed, armed, not detonated). Hidden traps are never shown and are left out of the capture. |
| Door | A green bar across each building doorway (`ABuildingExitMarker`s and the doorways `UInteriorExitSubsystem::FindExits` finds for each camera zone), with `EXIT` on the full map. Shown once explored. |
| Camera view | A white outline of the main camera's footprint on the shown floor (`MinimapRules::FrustumFootprint`). |

## 2. Floors

- **Data first**: the level JSON can list the upper floors: `"minimap": {"floors_cm": [z2, z3, ...]}` gives the world Z (cm) of each upper floor's walking surface. The list is sorted, near duplicates are dropped, and at most 3 upper floors are kept. `"bounds_cm": [min_x, min_y, max_x, max_y]` sets the mapped area. Without it the area is the union of the `ANavMeshBoundsVolume`s.
- **Automatic fallback** (`UMinimapSubsystem::DetectFloors`): a coarse grid (one column every <= 6 m, so any building of 6 m or more gets a column; at most 160 x 160) is traced top-down through every walkable surface (`ECC_WorldStatic`, normal Z >= 0.7, visibility-blocking only, so trigger boxes are skipped). A surface **stacked >= 2.2 m above the lowest surface of its column** is an upper-floor candidate. Every stacked coarse column is refined on a 2 m grid round it. `MinimapRules::DetectFloorBases` makes a 50 cm height histogram, keeps peaks with >= 3 samples that are >= 2.2 m apart (ramps and stairs spread over many bins and drop out), and refines each peak to the mean of its samples. A candidate is kept only if the squad can walk there, which means a full navmesh path from the leader to one of its samples. Flat roofs and closed lofts therefore are not floors.
- **Which floor an operative is on** (`MinimapRules::SelectFloor`, every 0.25 s): floor 0 if nothing walkable lies >= 1.1 m below his feet. A hill top is therefore always the ground, whatever its height. Otherwise he is on the highest upper floor k with feet >= base_k - 1.2 m, with a 30 cm hysteresis on the band edge against flicker on stairs. The map shows the selected operative's floor.
- **What each floor's capture leaves out** (`MinimapRules::HideOnFloor`, per static-mesh component):
  - Ground: anything whose bottom stands >= 2 m above the ground under it. This covers roofs, ceilings and upper storeys, and does not depend on the terrain.
  - Floor k: anything whose bottom is >= 2 m above that floor's surface (its roof, the floors above).
  - A building made as ONE mesh (all storeys plus the roof) cannot be cut this way. Gemini request #24 asks for a height clip.

## 3. Capture and drawing (perf)

- **Once per floor at level load** (about 1 s after begin play, after the navmesh build), `UMinimapSubsystem` spawns a transient `ASceneCapture2D`:
  - orthographic, at the start camera's yaw / pitch, set far back along the view so nothing is clipped;
  - `bCaptureEveryFrame` off, FinalColorLDR into an **sRGB RGBA8 render target**, longer side `Codex.Minimap.CaptureSize` (2048);
  - manual exposure `Codex.Minimap.ExposureBias` (0). A one-off capture has no exposure history, and auto exposure started 3+ stops too bright;
  - particles, fog, volumetric fog, motion blur, DoF and lens flares off;
  - hidden: pawns, markers, ghosts, mines / tripwires, runtime-spawned helpers (non-startup actors except static meshes), the `NoMinimap` tag, plus the floor's hidden components.
- `Codex.Minimap.CapturePasses` (4) frames, one capture per frame, so lighting can settle. Nothing is captured again unless the level changes: the dev command `CodexTactics.MinimapRecapture` redetects the floors and captures everything again.
- Headless (`-nullrhi`) the render targets exist and nothing is drawn.
- **Per frame**: the HUD (`ACodexTacticsHUD::DrawMinimap*`, `CodexTacticsHUDMinimap.cpp`) draws:
  - the visible UV window of the floor's image (one textured tile);
  - the fog as a quad on the floor's plane (the per-floor fog texture, clipped to the panel by `MinimapRules::ClipPolygonToRect`);
  - the camera footprint and the icons.
- **Fog**: a 2 m grid (at most 512 x 512 cells, cells grow on huge levels) over the XY footprint of the whole image on the floor planes, one bit layer per floor. Every 0.25 s each operative reveals a 15 m disc on his own floor. No line of sight is used, it is a pure disc. The per-floor texture is re-uploaded only when a cell changed. Hills are drawn with the fog on a flat plane, so on steep terrain the fog edge can be a few metres off.
- **Save**: `world_state.minimap` holds the panel mode and the fog (grid geometry and base64 per floor). A save from another grid (the level changed) is ignored. A load before the map was set up is applied once it is.

Measured (rendered `-RenderOffscreen`, this machine, 2026-10-18):

| | L_MovementTest (64 x 128 m) | L_Demo01_A_FrozenLake (560 x 329 m) |
|---|---|---|
| setup (area, floor scan, hidden lists, targets) | 4-6 ms (2 126 columns) | 6-7 ms (5 472 columns) |
| capture, game thread per pass | ~1.0 ms (2048 x 1316) | ~0.9 ms (2048 x 1213) |
| capture passes | 4 per floor at load | 4 |
| live update (fog, floors, enemy sight) | 30-80 us every 0.25 s | ~20 us every 0.25 s |
| icons per frame | 6-25 us | ~9 us |
| canvas drawing per frame | 28-41 us | ~30 us |
| memory | 2 x 10.7 MB (2 floors) + fog | 9.9 MB + fog 458 x 458 x 4 B |

The GPU cost of the 4 capture passes at load was not measured. Gemini request #24 covers it.

## 4. Code map

| Piece | Where |
|---|---|
| Pure rules (projection, view, zoom, frustum, clipping, floors, fog grid, enemy fade) | `Public/UI/MinimapRules.h`, `Private/UI/MinimapRules.cpp` |
| State, capture, floors, fog, icons, save | `UMinimapSubsystem` (`Public/UI/MinimapSubsystem.h`) |
| Drawing | `Private/UI/CodexTacticsHUDMinimap.cpp` (HUD unit); `DrawMessageFeed` starts below the header |
| Input | `Private/Core/CodexTacticsPlayerControllerMap.cpp` (new controller unit "Map"); hooks in `OnClick` (Selection), `CameraDragRotateStart` / wheel (Camera), `TabPressed` / Esc (Menus), F2 / F3 binds + hover guard (main) |
| Camera jump | `ATacticalCameraPawn::LookAtPoint` (holds the spot, no drift back); edge scroll off over the map |
| Level JSON | `FLevelCombatConfig::MinimapFloorsCm` / `MinimapBoundsCm` (`LevelJsonRules`, `"minimap"`) |
| Commands (F1 «Movement & camera») | `Map.Panel` F2, `Map.Full` Tab, `Map.Camera` LMB on map, `Map.Move` RMB on map (reported as `Order.Move`), `Map.Zoom` wheel on map; never tutorial-locked (camera-like aids) |
| Tests | `CodexTactics.UI.Minimap.*` (9): projection round trip / fit, view mapping, footprint + clipping, floor detection / selection / cut, fog reveal + persistence, enemy fade, level JSON |
| Smoke | `MinimapSmoke` (L_MovementTest); `MinimapSmoke Shot` stills `Saved/Screenshots/minimap_*_<map>.png`, raw capture `minimap_capture_<map>.png` |

## 5. Decisions taken (tell the user)

- **Tab** was not free: in turn-based combat it is "next operative". Tab opens the full map in every other mode, and **F3** opens it in any mode.
- **F2** switches the panel (F1 = controls, F5 / F9 = save / load; F2 / F3 were free).
- The panel starts on **MESSAGES** (`Codex.Minimap.DefaultPanel` 0) so the tutorial's feed lines stay visible. In map mode the header counts unread lines. Set the CVar to 1 to start on the map.
- The minimap commands are **never locked** by the tutorial. They are camera-like aids, and the RMB order is an `Order.Move` that the tutorial already gates.
- Fog **per floor**: walking the ground floor does not reveal the rooms upstairs.
- Unexplored areas keep a faint (8 %) silhouette of the level.
