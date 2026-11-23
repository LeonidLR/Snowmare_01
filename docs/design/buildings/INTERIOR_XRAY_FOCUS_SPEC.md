# Interior X-ray Focus - technical specification

Date 2026-11-23. Author: L4 architect pass for the Lead. Status: proposal, nothing implemented. Godot reference: none (UE-only).
Suggested home in the repo: `docs/design/INTERIOR_XRAY_FOCUS.md`; system doc section to add later: `docs/systems/camera-hud-minimap.md`.

## 0. Сводка для дизайнера

**Что вы увидите.** Когда выбранные бойцы находятся внутри здания, включается «рентген-фокус»:

- Экран слегка затемняется по краям (виньетка), в центре остаётся светлая зона вокруг выбранных бойцов.
- Вокруг бойцов появляется сфера. Все перегородки и ближняя к камере наружная стена внутри сферы становятся полупрозрачными
  «призраками» (около 12 % плотности) с бирюзовым свечением по кромке сферы и по граням стен.
- Сфера вытянута в сторону камеры: исчезает именно то, что заслоняет обзор. Повернули камеру (Q / E) - сфера повернулась за ней.
- Один выбранный боец - сфера вокруг него; двое - вокруг обоих; трое - вокруг всех троих. Если бойцы разошлись слишком далеко,
  у каждого своя сфера.
- Пол, лестницы, двери, мебель и укрытия остаются видимыми. Этажи выше по-прежнему убирает существующий «срез этажей».
- Сквозь «призрачную» стену можно кликать: по полу за ней, по двери, по врагу.

**Чем вы управляете.** Режим включается сам внутри здания и выключается на улице; клавиша (предлагаю CapsLock) выключает и включает
его вручную. Радиус, плотность «призрака», сила виньетки и цвет свечения лежат в одном файле настроек и меняются без пересборки.

**Чего режим не делает.** Это только картинка: стены остаются стенами для пуль, обзора бойцов и врагов, навигации и укрытий.
Враги, скрытые правилами видимости, не появляются. В автотестах и ночных прогонах режим выключен и на результаты не влияет.

**Нужны ваши решения** - пять вопросов в разделе 7, у каждого есть вариант по умолчанию.

---

## 1. Requirements and constraints

Requirements (designer 2026-11-23): indoors he cannot see enemies or click objects behind partitions even after rotating the camera. He
wants a slight full-screen vignette, a camera-dependent sphere around the SELECTED operatives (1 / 2 / 3 men) inside which partitions
turn (semi-)transparent with a fresnel look, and thin frames on doors / windows / stairs (the frames are task
`docs/port/codex/inbox/054-door-frame-icons.md`, not this spec).

Hard constraints:

1. Purely visual. No collision, Visibility-channel, navigation or save-state change. Sight and fire trace `ECC_Visibility`.
2. Inert in `-nullrhi` and in every `-unattended` run (smokes, bot, enemy-AI A/B), unless a smoke forces it on.
3. No destructive edit of the vendor pack `Content/SovietHouses`. No edit of any map (`L_MovementTest`, the designer's `L_Gen_Depot_1_X6`).
4. Kit walls are instanced: per-wall hiding by component is impossible on the dressed map.
5. Project rules: C++ first, no GAS, pure `*Rules` namespace + tests, JSON tuning, a headless smoke, assets made by `Scripts/Editor/create_*.py`.

### 1.1 Facts verified in the repo (main @ df6842b9)

| # | Fact | Where |
|---|---|---|
| F1 | Storey index and API: `GetOperativeStorey`, `GetStoreyPieces`, `GetStoreySlabs`, `GetStoreyBaseZ`, `IsCutAway`, `RefineCursorHit`, `TraceCursorRay`, `OnCutawayChanged`. Private: `StoreyOfPiece`, `FindOperativeStorey`, `GetStoreyCeilingZ`. The index holds hidden grey proxies too. | `Source/CodexTactics/Public/Camera/RoofCutawaySubsystem.h`, `Private/Camera/RoofCutawaySubsystem.cpp` |
| F2 | Every controller cursor trace goes through `ACodexTacticsPlayerController::GetCursorHit` -> `RefineCursorHit` (selected = TB active unit, else `USquadSubsystem::GetLeader`). | `Private/Core/CodexTacticsPlayerController.cpp:537-548` |
| F3 | Kit dressing: one actor `Kit_<id>` per building, one ISM component per (mesh, material, mirrored, group, storey), `NoCollision`, tags `kit_part=<part of its first instance>`, `building=<id>`, `floor=<n>`. Grey builder pieces it replaces stay as hidden collision proxies (tag `KitDressProxy`). | `Scripts/Editor/kit_dress.py:187-195, 288-308, 331-376` |
| F4 | Kit part names: `interior_wall`, `interior_stairwell_wall`, `facade_solid`, `facade_window_bay`, `facade_balcony_bay`, `facade_entrance_bay`, `facade_stairwell_bay`, `floor_plane`, `ceiling_plane` (stored on storey + 1), `roof_plane`, `door_leaf_open/closed`, `flat_door`, `entrance_door`, `stair_*`, `radiator`, `balcony`, `plinth`, `cornice`, `parapet`, fills / strips. | `Scripts/Editor/kit_dress_lib.py` |
| F5 | Pack masters: `M_SH_Master` (opaque, DefaultLit), `M_SH_Plain` (opaque, two-sided), `M_SH_Glass` (translucent), `M_SH_Emissive`; 31 `MI_SH_*` in `Materials/Instances` plus `Instances/Buildings/*`. Kit meshes use their own material slots; `mat` overrides slot 0 only for some parts. | `Content/SovietHouses/Materials/**`, `Content/Data/LevelGen/kit_series.json` |
| F6 | `Content/SovietHouses/` and `Content/Maps/KitPilot/` are untracked (`git status` shows `??`). The pack was already patched locally once by script (`ensure_ism_usage`). | `kit_dress.py:378-410` |
| F7 | Grey generator: engine cube / cylinder `StaticMeshActor`s with `/Engine/EngineMaterials/WorldGridMaterial`. Interior pieces carry `interior`, `building=`, `floor=`; exterior ones the wall kind (`building`) + `building=` + `floor=`; slabs `floor_slab` / `roof_slab`; stairs `stair=<id>`. Storey height 300 cm. | `Scripts/Editor/build_levelgen_map.py:80, 91, 1033, 1077, 1687` |
| F8 | Camera: perspective, vertical FOV 30 deg, pitch -35.26 deg, yaw in 45 deg steps, distance 8-40 m (base 26 m). | `Public/Camera/TacticalCameraRules.h` |
| F9 | Renderer config: only `r.AllowStaticLighting=False` and `r.Shadow.Virtual.Enable=1`. No GI method is set, and the engine default of `r.DynamicGlobalIlluminationMethod` is 0. `DefaultScalability.ini` (Gemini) uses CSM on Low / Medium, VSM on High / Epic. | `Config/DefaultEngine.ini`, `Config/DefaultScalability.ini` |
| F10 | All smokes and bot runs start with `-unattended`; `-nullrhi` unless the smoke is marked `rendered`. | `Scripts/smoke.ps1:23`, `Scripts/smoke_parallel.ps1:146-151`, `Scripts/bot_run.ps1:139` |
| F11 | Silhouette: `AOperativeCharacter::UpdateSilhouette` traces camera -> operative on `ECC_Visibility` and sets the overlay when blocked. | `Private/Characters/OperativeCharacterOverlays.cpp:271-313` |
| F12 | Reusable patterns: JSON rules (`WindowOrderRules` + `Content/Data/Interactables/windows.json`), MPC from C++ and `LoadIfPresent` (`USnowInteractionSubsystem`), unbound `UPostProcessComponent` with a blendable (`WeatherRainFxComponent.cpp:482-487`), custom primitive data (`CharacterSnowSubsystem.cpp:177`). | as named |

### 1.2 Facts that are NOT as the task brief states, or only probed

- **The Visibility fix is not in main.** `CutPiece` in main (`RoofCutawaySubsystem.cpp:995`) still sets `ECC_Visibility` to `ECR_Ignore`.
  The fix exists only as uncommitted edits in the worktree `C:/Work/Lanes/CodexTactics-fixes` (branch `claude/showcase-fixes`): CVar
  `Codex.RoofCutaway.CutIgnoresVisibility` (default 0) and a new `URoofCutawaySubsystem::LineTraceSkippingCut` for view-side traces.
  This spec builds on that fix; X-ray work starts after it is merged.
- **Nanite on the kit: probably off.** None of the 315 mesh packages under `Content/SovietHouses/Meshes` contains the names
  `NaniteSettings` / `bEnabled` (name-table probe; a default-valued struct is not serialized). Confirm in the editor (phase P0).
- **Lumen: probably off.** No project setting enables it; a map's PostProcessVolume could. Confirm on the showcase maps (P0).
- **Master graph contents are unknown** (parameters, static switches, emissive use, material slots per mesh). `.uasset`s were not opened.

---

## 2. Options analysis

Geometry that drives everything: at pitch 35.26 deg a 300 cm wall hides 300 / tan(35.26 deg) = 424 cm of floor behind it. One partition
hides most of the next room, so fading only "walls between camera and the man" is not enough; every partition in the focus must fade.

| Option | What | Cost | Risks | Verdict |
|---|---|---|---|---|
| **A. Material-side focus volume** | Wall materials compute the pixel's distance to a sphere / capsule passed in a Material Parameter Collection and dither their opacity mask; an emissive rim gives the fresnel look. | Small pixel cost (3 capsule distances). Materials become Masked. One shared Material Function. | Masked instead of opaque (depth / shadow passes run a pixel shader). Needs the function inside the pack's materials. Dither needs temporal AA to look smooth. | **Recommended, with the two refinements below.** |
| **B. Per-instance custom data on the ISMs** | C++ writes a fade value per wall instance each update. | CPU writes and GPU-scene uploads whenever the squad moves; still needs a Masked material. | Whole 3 m modules pop instead of a smooth sphere; the sphere edge cannot be drawn. | Rejected. Per-COMPONENT custom data is kept, to carry the piece class. |
| **B2. Re-split the kit ISMs per wall run and hide components** (Sims style) | `kit_dress` emits one component per wall; C++ hides those between camera and squad. | Re-dress every kit map; many more draw calls. | Hard pops, no partial fade, no sphere. | Rejected. |
| **C. Cut walls to waist height** (XCOM) | Clip wall pixels above about 110 cm inside the focus. | Same material work as A. | Kit walls have no cap geometry: the cut shows hollow walls and back faces. | Rejected as the main look. Kept as a tunable "base band" (the lowest part of a faded wall stays denser, so wall lines stay readable). |
| **D. Post-process / scene depth / custom stencil** | A post-process pass removes walls inside a screen circle. | One full-screen pass. | A post-process cannot show what was never rendered behind the wall. Stencil can only draw silhouettes of marked actors, not the floor, doors or furniture, so clicking stays blind. | Rejected for the reveal. Used for the vignette only. |
| **E. Second scene capture without walls**, composited in a circle | A SceneCapture renders the interior with wall components hidden. | A second scene render every frame. | Expensive, one frame of lag, AA mismatch, a 2D circle instead of a sphere, per-component hiding again. | Rejected. |
| **F. True translucency for walls** | Translucent twins instead of Masked. | Forward-lit translucency, overdraw. | Sorting errors between instances of one ISM, no depth write (breaks decals, outlines, DOF), lighting differs from the opaque wall so the swap is visible. | Rejected. |

Refinements that make A cheap and non-destructive (this is the "better idea" over plain A):

1. **Runtime material twins, only on the active storey.** The wall keeps its vendor material. While X-ray is active, C++ swaps the
   components of the leader's building and storey to Masked "twin" materials and swaps them back afterwards. Every other storey and
   building stays opaque, so the Masked cost is bounded to a few dozen components, and no map is rebuilt.
2. **Class by custom primitive data.** One float per component (0 none, 1 interior partition, 2 exterior wall) tells the shared function
   how to fade. C++ decides WHICH pieces fade (by tags and storey); the material decides WHERE (by world position).

**Recommendation: A + refinements 1 and 2, vignette as a post-process material, C as a tunable base band.**

---

## 3. Recommended design

### 3.1 New code units

| Unit | Path | Role |
|---|---|---|
| `InteriorXrayRules` + `FInteriorXrayTuning` | `Source/CodexTactics/Public/Camera/InteriorXrayRules.h`, `Private/Camera/InteriorXrayRules.cpp` | Pure rules and JSON tuning (`Get` / `Reload` / `Parse` / `FilePath`, as `WindowOrderRules`). |
| `UInteriorXraySubsystem` | `Public/Camera/InteriorXraySubsystem.h`, `Private/Camera/InteriorXraySubsystem.cpp` | `UTickableWorldSubsystem` (Game / PIE). Holds only transient view state. |
| Tuning | `Content/Data/Camera/interior_xray.json` | All numbers below. |
| Tests | `Source/CodexTacticsTests/Private/Camera/InteriorXrayRulesTest.cpp` | `CodexTactics.Camera.InteriorXray.*` |
| Smoke | `Source/CodexTacticsDev/Private/Debug/InteriorXraySmokeCommand.cpp` | `CodexTactics.InteriorXraySmoke`, entries in `Scripts/test_map.json`. |
| Asset script | `Scripts/Editor/create_xray_focus_assets.py` | Section 4. |

### 3.2 When it is on

Active when all of these hold:

- `Codex.Xray` allows it (section 3.9) and the player's toggle is on.
- The cutaway is on (`Codex.RoofCutaway`, `Codex.RoofCutaway.Storeys`) and `URoofCutawaySubsystem::GetOperativeStorey(Leader)` puts
  the leader in an indexed building. Maps without `building=` / `floor=` tags (for example `L_MovementTest`) get no X-ray in v1.
- The leader is not inside a shown fixed zone camera (`ACameraZoneVolume::IsZoneCameraShown`; same rule as `ShouldCutForOperative`).
- The leader is alive, not hidden, not mounted (the cutaway's own filter in `UpdateNow`).

The X-ray storey is always the cutaway's storey: the leader's building and storey. This keeps one source of truth and inherits the
cutaway's stair / doorway hysteresis (`StoreyKeepInsideCm`). Activation alpha fades in over `fade_in_s` and out over `fade_out_s`,
on real time (`DeltaRealTimeSeconds`), so it works during the tactical pause like the cutaway.

### 3.3 Focus volume

Focus operatives = `USquadSubsystem::GetSelectedGroup()` (just the leader when there is no box selection; the active unit in turn-based),
filtered to living, visible men whom `GetOperativeStorey` puts in the leader's building AND on the leader's storey. A selected man on
another storey or outside gets no bubble; the existing silhouette covers him. Showing a man on a lower storey would need faded floors,
which this spec excludes.

`InteriorXrayRules::ComputeFocus(Points, Tuning)` returns 1 to 3 spheres:

1. Centre = centre of the smallest circle enclosing the points in XY. Z = storey base (`GetStoreyBaseZ`) + `centre_height_cm` (120).
2. Radius = clamp(enclosing radius + `radius_padding_cm` (250), `radius_min_cm` (450), `radius_max_cm` (900)).
3. If enclosing radius + padding exceeds `radius_max_cm`, the squad is spread out: `split_mode` `per_operative` (default) gives each man
   his own sphere of `radius_min_cm`; `leader_only` keeps one sphere on the leader.

Camera dependence - the "lean". Each sphere is a capsule from its centre A to B = A + H x L, where H is the horizontal unit vector from
the focus towards the camera (`PlayerCameraManager->GetCameraLocation()`) and

    L = min(lean_max_cm, lean_scale x StoreyHeight / tan(elevation of the camera above the focus))

With the default camera L = 424 cm (clamped to `lean_max_cm` 450); looking straight down L = 0. The volume therefore sits between the
camera and the squad and swings round when the camera rotates. `InteriorXrayRules::LeanLength` is pure and tested.

Smoothing, every frame: centre and radius follow their targets (`follow_speed` 8 /s, `radius_speed` 5 /s). A target jump larger than
`retarget_snap_cm` (600; the player selected a far man) fades alpha to 0, moves the sphere, and fades back in, instead of sliding the
bubble through the building.

### 3.4 Which surfaces fade

`InteriorXrayRules::ClassifyPiece(ComponentTags, ActorTags)` returns None / Interior / Exterior. It is the single table used for both
the visual swap and the cursor rule.

| Piece | Kit (`kit_part=`) | Grey (tags) | Class | Why |
|---|---|---|---|---|
| Interior partitions, their lintels | `interior_wall`, `interior_stairwell_wall`, `door_head_fill`, `door_jamb_fill` | `interior` | Interior | The designer's request. |
| Exterior walls of the storey | `facade_*`, `window_jamb_fill`, `entrance_top_strip`, `stairwell_top_strip` | wall kind `building`, or `opening_lintel` without `interior` | Exterior | Only the half on the camera side of the focus fades (a gate on the dot product with H, soft over `exterior_gate_soft_cm`). The far wall does not hide the interior and keeps the building readable. |
| Floors, ceilings, roof | `floor_plane`, `ceiling_plane`, `roof_plane` | `floor_slab`, `roof_slab` | None | The floor is what the player clicks and what places the squad in space. Ceilings and the roof are already cut by the storey cutaway. |
| Stairs | `stair_*` | `stair=<id>` | None | Important object; 054 frames it. |
| Door leaves, windows panes, door frames | `door_leaf_*`, `flat_door`, `entrance_door`, `entrance_frame`, `window`, `balcony_door_window` | door-state pieces, `ADoorActor` | None | Must stay visible and clickable through a faded wall. |
| Furniture, cover props, radiators, rubble | `radiator`, props | prop actors | None in v1 | Cover must stay readable; most props are low; their materials are arbitrary (no twin). A later opt-in tag `XrayFade` for tall props is listed in P6. |
| Characters, vehicles, loot, traps | - | - | None | Never touched. |

Any part or tag not listed is None (`porch`, `canopy`, `drain`, `stairwell_glass`, ...). The classes of the fill / strip parts are a
proposal; confirm them on the map in P0.

Only pieces of the leader's building whose storey equals the leader's are eligible, taken from `GetStoreyPieces(Building)`. Pieces the
cutaway has cut (`IsCutAway`) are never touched. Hidden proxies are classified (for the cursor rule) but not swapped.

Known gap to check in P0: a kit component's `kit_part` tag comes from its first instance, and `ism_key` does not include the part. If the
audit finds a component mixing classes, add the class to `ism_key` in `kit_dress.py` (Claude) and re-dress; otherwise no kit change.

### 3.5 Fade look (Material Function `MF_XrayFocus`)

Inputs: pixel absolute world position P and world normal N, the collection `MPC_XrayFocus`, scalar `XrayClass` read from custom
primitive data index `InteriorXrayRules::CustomDataIndex` (0; default value 0 = no fade).

    d_i     = distance from P to the segment (Sphere_i.xyz, Sphere_i.xyz + Lean.xyz)           i = 0..2, radius = Sphere_i.w
    reveal  = max_i (1 - smoothstep(radius_i - EdgeSoft, radius_i, d_i)) x Alpha x (XrayClass > 0)
    reveal *= exterior gate (only when XrayClass = 2)
    opacity = lerp(1, P.z < FloorZ + BaseBand ? BaseBandOpacity : InnerOpacity, reveal)
    rim     = 4 x reveal x (1 - reveal)                                    (peaks on the sphere surface)
    fresnel = (1 - saturate(N . V)) ^ FresnelPower x reveal
    OpacityMask = ShadowPassSwitch(default: opacity, shadow: 1)
    Emissive    = RimColor x (RimIntensity x rim + FresnelIntensity x fresnel)

Decisions:

- **Dithered opacity mask, not translucency.** The twins are Masked with "Dithered Opacity Mask" on; with TSR / TAA this reads as a
  stable 12 % ghost. Depth is still written, so lighting, decals, the hover frames and the silhouette keep working, and there is no sorting.
- **Without temporal AA** (FXAA, or shot commands that set `r.AntiAliasingMethod 0`) dither is a visible stipple. The subsystem then uses
  `inner_opacity_no_taa` (0: a clean cut-out).
- **Shadows unchanged.** `ShadowPassSwitch` keeps the wall solid in shadow passes: lighting in the room does not jump as the bubble
  moves, and cached Virtual Shadow Map pages stay valid (a mask driven by a collection would not invalidate them).
- **Fresnel.** The rim band draws the sphere's outline where it cuts walls; the N.V term brightens the grazing wall faces. Colour is the
  teal of the 054 frames (`rim_color`). Optional polish: a thin floor ring at the sphere radius through `URadiusRingSubsystem` (`floor_ring` 0).
- **Base band.** The lowest `base_band_cm` (40) of a faded wall stays at `base_band_opacity` (0.5), so the player still sees where walls
  stand and where he cannot walk. Set to 0 to disable.
- The function is one `Custom` (HLSL) node plus the collection reads, so the script can build it. At Alpha 0 or class 0 a twin renders
  exactly like its original, so the swap itself is invisible.

### 3.6 Vignette

A post-process material `M_PP_XrayVignette` (blendable location "after tonemapping") on an unbound `UPostProcessComponent` owned by a
transient actor the subsystem spawns (pattern: `WeatherRainFxComponent` screen drops). Blend weight = activation alpha. It reads from
the collection the focus centre projected to screen UV, its screen radius and the aspect ratio, written by the subsystem each frame.

    dark = vignette_strength (0.25) x smoothstep(R, R x vignette_falloff (1.8), aspect-corrected distance from the focus UV)
    colour = lerp(colour, desaturated colour, vignette_desaturate (0.15) x dark) x (1 - dark)

Screen-space on purpose: stable under TSR, no depth read, and the HUD is not darkened (a UMG overlay like `UFrostVignetteWidget` would
darken the canvas HUD drawn under it). With several spheres the vignette uses the circle enclosing them.

### 3.7 Runtime application and the cutaway

Slow pass (every 0.1 s real time, the cutaway's cadence):

1. Resolve leader, building, storey; build the eligible set (cached per building + storey; rebuilt when the storey index is rebuilt -
   add a generation counter `URoofCutawaySubsystem::GetStoreyIndexGeneration()`).
2. For each newly eligible visible component: remember its `OverrideMaterials` state per slot (override or none - not `GetMaterial()`),
   then for every slot whose material has a twin, `SetMaterial(slot, twin)`; set custom primitive data to the class.
3. For each component no longer eligible for `restore_delay_s` (1.0): put back the remembered override state and custom data 0.
4. `Deinitialize`, `Codex.Xray 0`, the cutaway switched off: restore everything at once (mirror of `RestoreAll`).

Twin lookup is pure (`InteriorXrayRules::TwinPath(OriginalPath, Tuning)`, rules in the JSON), loaded if present and cached; a missing
twin leaves that slot untouched and logs one warning naming the asset script. Translucent glass has no twin in v1.

Fast pass (every frame): smoothing, lean, collection writes (skipped when nothing changed), post-process weight.

Order with the cutaway: the cutaway hides whole components of upper storeys through render flags; X-ray swaps materials on the leader's
storey. The two sets are disjoint by construction (`ShouldCutStorey` cuts only storeys above his). On a storey change the old set is
restored and the new one swapped; a minimum dwell of 0.5 s on a storey avoids thrash at a stair head.

### 3.8 Gameplay untouched; view-side traces

- The subsystem never calls a collision setter. Kit components are `NoCollision` already; grey pieces keep every response. The smoke
  snapshots and compares collision profile and channel responses of every touched component.
- Sight, fire, explosions, AI perception and navigation use the same geometry as before. A faded wall blocks a bullet.
- X-ray does not un-hide anything: actors hidden by sight / concealment rules stay hidden. It removes camera occlusion only.

Two view-side traces must agree with what the player sees. Both get one shared predicate
`UInteriorXraySubsystem::IsViewOpen(const FHitResult&)`: true when X-ray is active, the hit component is in the leader's building and
storey (new public accessor `URoofCutawaySubsystem::GetPieceStorey`), its class is not None, and `InteriorXrayRules::RevealAt(hit point)`
is at least `click_through_min_reveal` (0.5). `RevealAt` is the CPU mirror of the function in 3.5.

1. **Cursor.** `RefineCursorHit`'s skip rule becomes "cutaway rule OR `IsViewOpen`". Result: a click on a faded wall lands on the floor,
   door, stair, loot or enemy behind it; a wall outside the bubble behaves as today. On the dressed map the ray hits the hidden grey
   proxy, which carries the grey tags, so the same classification works. Doors, stairs and floors are class None and are never skipped.
   An attack click on an enemy seen through a faded wall is accepted as an order; whether he can be shot is still decided by the
   unchanged `ECC_Visibility` sight line. CVar `Codex.Xray.ClickThrough` 0 gives the raw behaviour (A/B). `TraceCursorRay` gets the
   same rule, so smokes can test it.
2. **Silhouette.** `UpdateSilhouette` (and `LineTraceSkippingCut` once the fix is merged) also skips `IsViewOpen` hits; otherwise an
   operative visible through a ghost wall would wear the "behind a wall" overlay.

Consequence to accept (open question 2): inside the bubble a wall cannot be clicked, so "take cover at this wall" needs the toggle off
or a click on the wall outside the bubble.

### 3.9 Tuning, console variables, headless behaviour

`Content/Data/Camera/interior_xray.json` (`"schema": "codex.interior_xray/1"`; missing keys keep defaults):

| Group | Keys (default) |
|---|---|
| Volume | `radius_min_cm` 450, `radius_max_cm` 900, `radius_padding_cm` 250, `centre_height_cm` 120, `split_mode` "per_operative", `lean_scale` 1.0, `lean_max_cm` 450 |
| Motion | `fade_in_s` 0.2, `fade_out_s` 0.3, `follow_speed` 8, `radius_speed` 5, `retarget_snap_cm` 600, `restore_delay_s` 1.0, `storey_dwell_s` 0.5 |
| Look | `inner_opacity` 0.12, `inner_opacity_no_taa` 0, `edge_soft_cm` 60, `base_band_cm` 40, `base_band_opacity` 0.5, `rim_color` [0.25, 0.85, 0.9], `rim_intensity` 2.0, `fresnel_power` 3, `fresnel_intensity` 0.6, `exterior_gate_soft_cm` 100, `floor_ring` 0 |
| Vignette | `vignette_strength` 0.25, `vignette_falloff` 1.8, `vignette_desaturate` 0.15 |
| Input | `click_through` 1, `click_through_min_reveal` 0.5 |
| Assets | `twin_rules`: [{from_root "/Game/SovietHouses/Materials", to_root "/Game/SovietHouses/Xray", suffix "_Xray"}, {from "/Engine/EngineMaterials/WorldGridMaterial", to "/Game/Environment/Buildings/Xray/M_WorldGrid_Xray"}] |

Console:

| CVar / command | Meaning |
|---|---|
| `Codex.Xray` | 0 off. 1 auto (default): on only in an interactive rendered session - off when `!FApp::CanEverRender()` or `FApp::IsUnattended()`. 2 forced on, logic also headless (smokes only). |
| `Codex.Xray.ClickThrough` | 1 / 0, A/B of the cursor rule. |
| `Codex.Xray.Vignette`, `Codex.Xray.Radius`, `Codex.Xray.InnerOpacity` | -1 = JSON value; otherwise an override for live tuning. |
| `Codex.Xray.Debug` | 1 draws the capsules and the eligible components' bounds, logs swaps. |
| `Codex.Xray.Reload` | Re-reads the JSON. |

Determinism: every existing smoke, bot run and A/B run is `-unattended` (F10), so with the default `Codex.Xray 1` the subsystem does
nothing there - no material swap, no cursor rule, no trace. It keeps no gameplay state, uses no random numbers, writes nothing to saves
and advances on real time only. Existing rendered shot commands keep their pictures for the same reason.

Player toggle: an input action `IA_XrayFocus` in `ACodexTacticsPlayerController::CreateInputActions`. All letter keys are taken in
`CodexTacticsPlayerController*.cpp`; CapsLock is not bound anywhere in `Source/` (grep). The state lasts for the session.

---

## 4. Asset and material work list

Tracked, Claude-owned, under `/Game/Environment/Buildings/Xray/` (not `Content/VFX`, which is Gemini's). Claim them in `docs/port/TANDEM.md`.

| Asset | Content |
|---|---|
| `MPC_XrayFocus` | Vectors `Sphere0..2` (xyz centre, w radius; w = 0 unused), `Lean` (xyz), `CameraDirH`, `RimColor`, `FocusScreen` (u, v, radius, aspect). Scalars `Alpha`, `InnerOpacity`, `EdgeSoft`, `BaseBand`, `BaseBandOpacity`, `FloorZ`, `RimIntensity`, `FresnelPower`, `FresnelIntensity`, `ExteriorGateSoft`, `VignetteStrength`, `VignetteFalloff`, `VignetteDesaturate`. |
| `MF_XrayFocus` | Section 3.5. Outputs `OpacityMask`, `Emissive`. |
| `M_WorldGrid_Xray` | Duplicate of the engine's `WorldGridMaterial` (engine content, allowed in the project) + the function, Masked, dithered. The grey twin. |
| `M_PP_XrayVignette` | Section 3.6. |

Untracked, generated next to the pack by the script, under `/Game/SovietHouses/Xray/` (the vendor files are never modified):

| Asset | How |
|---|---|
| `M_SH_Master_Xray`, `M_SH_Plain_Xray` | Duplicate of the vendor master; blend mode Masked, dithered opacity mask on; `MF_XrayFocus.OpacityMask` into Opacity Mask; `MF_XrayFocus.Emissive` added to the existing emissive input; "Used with Instanced Static Meshes" kept. Two-sided flag unchanged. |
| `MI_SH_<Name>_Xray` (every instance whose root parent is a twinned master) | Duplicate of the vendor instance, parent set to the twin of its parent (parents first). Keeps every parameter and static switch. |

`Scripts/Editor/create_xray_focus_assets.py` is idempotent (creates what is missing, refreshes twins when the source is newer), runs as an
editor commandlet, writes `Saved/Xray/xray_assets_report.json` (twins made, masters without a twin, slots per kit mesh) and must be
re-run on every machine that has the pack and after a pack update. It never saves a package under `/Game/SovietHouses/Materials` or `/Engine`.

Why duplicates and not a parent swap or an in-place edit: an in-place edit modifies vendor files and makes every kit surface Masked
everywhere; re-parenting the vendor instances also edits vendor files. Duplicates keep the originals byte-identical, give exact visual
parity, and the runtime swap decides where the Masked cost is paid. Packaging note: twins are referenced by soft path only, so both
`Xray` folders must be added to "Additional Asset Directories to Cook" before the first packaged build.

Collection limit: a material may reference at most two different collections. The twins use one. If a later snow / weather layer for
building materials needs `MPC_Environment` and `MPC_WeatherSettings`, X-ray must move its vectors to custom primitive data (36 floats per
component are available). Record this in the snow-layer design.

---

## 5. Implementation phases

Order: P0 -> P1 (parallel with P2 asset work) -> P2 -> P3 -> P4 -> P5. One task at a time per agent; each phase ends with build + tests
+ `python Scripts/Tools/typesafe_triage.py --audit-diff --agent <agent>`.

**P0. Editor audit (Claude, needs the editor, 0.5 day).** Prerequisite: the `claude/showcase-fixes` Visibility fix merged.
- Record: parameters / static switches / emissive use of the four masters; material slots per kit mesh used by classes Interior and
  Exterior; Nanite flag of kit meshes; AA method and GI method active on both showcase maps; `kit_part` histogram per component (mixed classes?).
- Acceptance: a table appended to this spec; a go / no-go on "twins by duplicate" (fallback if a master cannot be duplicated cleanly: a
  project-authored master with the same parameter names).

**P1. Pure rules, tuning, tests (Codex, no `.uasset`, 1 day).**
- Files: `InteriorXrayRules.h/.cpp`, `Content/Data/Camera/interior_xray.json`, `InteriorXrayRulesTest.cpp`.
- Functions: `Parse` / `Get` / `Reload` / `FilePath`, `ComputeFocus`, `LeanLength`, `RevealAt`, `ClassifyPiece`, `IsEligible`,
  `TwinPath`, `ShouldSkipViewHit`, `IsAutoActive(bCanRender, bUnattended, CVarValue)`, `ScreenCircle`.
- Tests (all must pass with `Scripts/test.ps1 -Filter CodexTactics.Camera.InteriorXray`):
  `Tuning.ParseDefaults`, `Tuning.FileParses`, `Focus.OneOperative`, `Focus.TwoEnclosed`, `Focus.ThreeEnclosed`, `Focus.RadiusClamped`,
  `Focus.SpreadSplitsPerOperative`, `Focus.SpreadLeaderOnly`, `Lean.DefaultCamera424`, `Lean.TopDownZero`, `Lean.Clamped`,
  `Reveal.InsideEdgeOutside`, `Reveal.CapsuleTowardsCamera`, `Reveal.ExteriorCameraSideOnly`, `Reveal.BaseBand`,
  `Classify.KitParts`, `Classify.GreyTags`, `Classify.FloorStairDoorNever`, `Eligible.OnlyLeaderStorey`,
  `Cursor.SkipsRevealedWall`, `Cursor.KeepsWallOutsideBubble`, `Cursor.KeepsDoorStairFloor`, `Twin.PathRules`,
  `Activation.UnattendedOff`, `Activation.NoRenderOff`, `Activation.ForcedOn`.
- Fixture: real tag sets of both showcase maps (dumped by a dev command or copied from `Saved/KitBlocks/kit_dress_report_*.json`).

**P2. Assets, subsystem, view-side traces (Claude, needs the editor, 2 days).**
- Files: `create_xray_focus_assets.py`, the four tracked assets, `InteriorXraySubsystem.h/.cpp`; small additions to
  `RoofCutawaySubsystem` (`GetPieceStorey`, `GetStoreyIndexGeneration`, the extra skip rule in `RefineCursorHit` / `LineTraceSkippingCut`);
  `OperativeCharacterOverlays.cpp` (silhouette); `TANDEM.md` claim; `docs/systems/camera-hud-minimap.md`.
- Acceptance: `Scripts/build.ps1`; all `CodexTactics.Camera.*` tests; existing smokes unchanged
  (`RoofCutawayStoreySmoke`, `CutawayClickSmoke`, `WindowStairHoverSmoke`, `DoorsSmoke`, `FormationStairSmoke`) - they run unattended, so X-ray is off;
  screenshots on `/Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit` and `/Game/Maps/Generated/L_Gen_StoryShowcase`, building `S04_rubble_escape`,
  storeys 0 and 1: off / on with 1, 2, 3 men selected, and four camera yaws (TSR on).

**P3. Vignette, toggle, polish (Claude, 0.5-1 day).**
- Files: `M_PP_XrayVignette` wiring, `IA_XrayFocus` in `CodexTacticsPlayerController.cpp`, optional floor ring.
- Acceptance: vignette weight follows alpha; HUD brightness unchanged in a before / after screenshot pair; toggle works in the tactical
  pause; regenerate the console commands page (`Scripts/Tools/build_console_page.py`).

**P4. Smoke and frames integration (Codex, 1 day; after P2 and after 054 is merged).**
- File: `InteriorXraySmokeCommand.cpp`, `Scripts/test_map.json` (grey and dressed entries, `Codex.Xray 2` set by the smoke itself).
- The smoke asserts, per map and storey:
  A. squad outside: inactive, 0 components swapped.
  B. leader inside: active; every swapped component is in his building and on his storey and has class Interior / Exterior; no floor,
     stair or door piece is swapped; cut pieces untouched.
  C. collision profile and responses of every touched component equal the snapshot; an `ECC_Visibility` trace through a revealed
     partition is blocked with X-ray on exactly as with it off.
  D. cursor rays (`TraceCursorRay`) at floor points behind a revealed partition hit the floor of his storey with `ClickThrough` 1 and
     the wall with 0; rays at a door leaf and a stair inside the bubble still hit them; a wall outside the bubble is still hit.
  E. 1 / 2 / 3 selected men: one sphere containing all of them; men 20 m apart: split per tuning.
  F. storey change: the old set fully restored (override state and custom data), the new set swapped.
  G. leaving the building, `Codex.Xray 0`, world teardown: nothing left swapped anywhere in the building.
  `shots=1` (rendered, local) writes the P2 picture set.
- Frames: when X-ray is active, doors / windows / stairs inside the bubble show 054's frame permanently in a dimmer style (a pool of
  `AOpeningHighlightActor`s; hover keeps the bright one).
- Acceptance: `Scripts/smoke_parallel.ps1 -Commands InteriorXraySmoke` PASS on the worker (grey map, headless, forced) and locally (dressed).

**P5. Profiling and scalability (Gemini, 0.5 day).**
- On the dressed showcase, leader inside `S04_rubble_escape`, 1080p, each scalability tier: GPU ms with X-ray off / on (budget: at
  most +0.3 ms on High), quad overdraw inside the bubble, hitch on the first swap and on a storey change (budget 2 ms; if PSO
  compilation shows, pre-warm the twins at level load), VSM page invalidations while the bubble moves (expected none), Lumen surface
  cache artefacts if Lumen is on.
- Output: a short report and, if needed, per-tier keys (`DefaultScalability.ini` is Gemini's): vignette off and `inner_opacity` 0 on Low.

**P6. Later, optional.** Tall props through an `XrayFade` tag; glass twin; bake twins in `kit_dress` if profiling shows the swap is not
worth it; X-ray for a squad standing outside behind a building.

---

## 6. Technical risks

| Risk | Mitigation |
|---|---|
| Twins exist only on machines where the script ran (pack untracked). | Missing twin = that slot is not faded, one warning. The grey map works everywhere. Add the script to the PC-format checklist. |
| Masked + Nanite, if kit meshes are converted later. | The runtime swap bounds it to one storey. `ShadowPassSwitch` support on Nanite must be re-checked then. |
| The cursor rule and the picture drift apart. | One function `RevealAt` mirrors the material maths; test `Reveal.*`; smoke stage D. |
| A swapped component is left behind. | Restore on `Deinitialize`, CVar off, cutaway off; smoke stages F and G compare against the snapshot. |
| Material swap on a component another system also overrides (future destruction). | Remember and restore the override state, not the resolved material; document the rule next to `CutPiece`. |
| Dither stipple without temporal AA. | `inner_opacity_no_taa` 0. |

## 7. Open questions for the designer

1. **Automatic or on a key?** Default: automatic inside buildings, CapsLock switches it off / on for the session.
2. **Do clicks pass through a ghost wall?** Default: yes - floor, doors, loot and enemies behind it are clickable; "take cover at this
   wall" needs the toggle off or a click outside the bubble.
3. **Squad spread out or on different storeys.** Default: one bubble per man (up to 3) when they do not fit a 9 m radius; a selected man
   on another storey gets no bubble, only the existing silhouette.
4. **How much wall remains.** Default: 12 % ghost with a teal rim and a 40 cm denser base line; furniture and cover are not faded.
   Alternative: fully transparent walls (`inner_opacity` 0, `base_band_cm` 0).
5. **Outside, behind a building.** Default: no X-ray outdoors in v1. Frames on every door / window / stair inside the bubble: yes, dimmer
   than the hover frame, after 054 is merged.
