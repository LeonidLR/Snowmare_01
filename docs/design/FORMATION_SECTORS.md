# Formation look sectors & tactical movement — design decisions (session 2026-10-25)

Status: **design agreed with the user, no code yet.** Next session (local, with the model router / Jev / Unreal) starts here.
Visual reference with interactive schemes and 3-second clips: `docs/design/formation-sectors.html` (open in a browser;
same page as the claude.ai artifact https://claude.ai/artifact/ErDQfTKvYcV1Q3YzaA19HT). Keep the page, the clips and this
file in sync with every design change (user request: "update the animations with every fix").

## Core rule
An operative sees only his vision cone (already in game: 140° standing / crouched, 110° prone; out of cover the cone is
the body facing, `AOperativeCharacter::GetSightFacing`). The formation decides where each one looks. 12 o'clock = the
leader's heading.

## Roles and sectors (decided)
| Role | Slot (from the leader, m) | Walk = patrol | Run (double click) | Short halt |
|---|---|---|---|---|
| Commander (K) | 0, 0 | 12, light search 11–1 | 12 | 12 |
| Right (P) | 2.6 right, 2.8 back | scans 12 → 3, dwell on points | 12 | 2 |
| Left (L) | 2.6 left, 2.8 back | scans 9 → 12, counter-phase | 12 | 10 |
| Rear (Z), 4th member | 5.6 straight back (today slot 2 = 2nd row LEFT — change) | 6, walks BACKWARDS | 12 | 6 |

- 3 men = wedge (triangle); 4 men = **diamond** (rear centred). No 2×2 square.
- **Patrol pace: the whole squad walks at ~80 % of walk speed** so the rear man keeps up walking backwards.
- **Triad keeps its rear gap** (5–7 o'clock, ~40°): the honest cost of three men; the 4th closes it.
- Run: everybody faces 12 (rear blind ~220° — the price of speed).
- Halt: everybody turns to the centre of his sector.
- Box select + click: unchanged (each to his own point, `SquadFormation::ComputeGroupTargets`), faces his own path.
- Facing priority: combat target > explicit facing order > cover > formation sector > movement direction.
- Pair (2): partner behind the right shoulder, sweeps 3 → 6 → 9.

## Wall (group cover order in formation mode) — decided after the user's correction
Nobody looks through the wall. All stand along the wall (1 m apart) facing the OPEN side, the open half-plane is shared:
- 3: L on the left looks 8 (left flank along the wall), K on the clicked slot looks **6 (away from the wall)**, P looks 4.
- 4: the 4th stands at the end of the line **on the side with more wall room** (tie → the side he reaches with less
  travel); he takes the flank along the wall (3–4 / 8–9), his neighbour turns toward the centre (4 → 5 / 8 → 7);
  K and the other flanker unchanged.
- 2: K 7:30, partner 4:30.
- Needs a new pose: standing / crouched **back to the wall facing out** (today a cover operative is always shoulder to the
  wall looking along it, `CoverFacingRules`). Baseline: crouch idle facing out; the user replaces it with a clip later.

## Formations (kept)
Diamond / wedge (default), **column** — narrow vs wide (user decision 2026-10-25, Q1 = A): in a **narrow passage** (free width
below `column.narrow_width`, 6 m) the side men do not look into the walls: P looks **forward over the leader's shoulder
(+15°)**, L **back at a slant (−165°)**, Z straight back (180), K 12; the **"herringbone"** (K 0, P +70, L −70, Z 180) stays only
for a **wide column** (a manual column in the open / a wide passage). Line (all forward with a slight fan), **echelon right**
(obstacle on the left, all guns to the open right flank; from the diamond the LEFT man goes second — minimal travel; sectors
K 10, 2nd 45, 3rd 80, **last 150** — user decision 2026-10-25, Q2 = A: the last man covers the rear-right so 5–7 o'clock stays
watched), **echelon left** (mirror; the RIGHT man goes second), hedgehog at a heat source (backs to the fire, faces out —
WarmIdle link), blizzard tight diamond (1.1–1.2 m, shorter cones). Dropped by the user: bounding overwatch, door stack.

## Decisions after the bot's first runs (2026-10-25, via the coordinator)
- **Q1 = A** (user): narrow column = P forward over the shoulder, L back at a slant, Z back; herringbone only in a wide column.
- **Q2 = A** (user): echelon last man 150° (was 125°); the rear-cover check stays in force for the echelon.
- **Q3** (coordinator, classifier bug): a dead-end wall ahead no longer reads as side walls (classifier v1.1: ahead rays first,
  side probes ignore walls within 30° of perpendicular to the heading) — no column / echelon flip before the wall layout.
- **v1.2** (coordinator, design-doc fidelity): the leader's patrol search is ±30° (11–1 o'clock, as above); "All forward" (menu
  10:30) = walking with everyone looking at 12 in any formation; the wall layout triggers only on ARRIVAL at a wall (not when
  walking past it or turning at a bend); the hedgehog is centred on the heat source; a column of three keeps the triad's open
  rear in the wide herringbone (in a narrow column L's back-slant look covers it).
- **Patrol pace (user, 2026-10-25, "Вариант А. Потом будем смотреть.")**: the squad keeps ~80 % of the walk speed on patrol
  (`pace.patrol` 0.8, unchanged) and the rear man walks backwards at up to 90 % of the walk speed (`move.backward_mult` 0.9),
  so he keeps up with a margin (the first training preferred 0.7 / 1.0; to be revisited).
- **v2 (coordinator, 2026-10-25): the formation switch is held by the leader's PATH, not time** — a tighter situation (a wall
  beside, a passage, arrival at a wall) switches at once; a looser one only after 5 m of path with it, so short gaps never flip
  the echelon and a column keeps its narrow sectors until its tail has left the passage.
- **Wide passage = column (user, 2026-10-25, "3А")**: ANY passage between two walls (both sides walled within the probe reach,
  4 m) is a column — narrow sectors below 6 m (above), the ±70° herringbone in a wider one (e.g. 7.6 m); the diamond no longer
  walks a wide street with its side men scanning into walls 1.2 m away.
- **Blizzard (user, 2026-10-25, "4А")**: in a blizzard the squad picks the tight blizzard diamond automatically on OPEN ground;
  walls and passages keep their normal rules; the hold-G menu still overrides.

## v2.1 — the squad never splits (user decisions 2026-10-25, via the coordinator; contract section 10)
- **The bug the user found**: lab lane B, 4 men, manual "echelon left" with the wall on the left — the echelon's slots went
  through the wall, P / L / Z stood on its far side.
- **Never split (user, 2026-10-25: "они должны переходить либо в эшелон вправо, либо идти ёлочкой, обходить эту стенку справа
  или слева")**: everyone passes on the leader's side of a wall. A formation a wall cuts (any, manual or auto) takes the mirror
  echelon if that is whole, else the column — along the leader's own path (it bends round a corner and wraps a wall end), narrow
  or herringbone by the room each man has. It folds BEFORE a passage entrance (checked 5 m ahead on the path). The manual choice
  is not cleared: it comes back by itself after 5 m of path with it whole again (no flicker). The HUD shows
  "Эшелон влево → колонна (стена)".
- **The leader's side = "Б" (user, 2026-10-25)**: the shortest path round the wall by default; with a known threat (seen, or
  heard / last known position within 20 s) the side where the wall stands between the squad and the threat, if that detour is
  at most 30 % longer; otherwise the shortest.
- **Corner check = yes (user, 2026-10-25, "угол да")**: slicing the pie — nearing the free end of a wall the squad passes, the man
  closest to it looks round the corner (from 3 m before the end until 2 m past it; at a corridor exit one man per corner). It
  overrides his patrol sector only; a combat target and explicit facing orders keep priority.
- Bot: check 7 (squad split by a wall), 8 (passes on the shielded side), 9 (corner man in time); `--manual all` runs auto and every
  menu formation on every lab route; lab lane G has the free-standing walls and threats.

## Selection
- **Automatic by level geometry** (default): open ground → diamond / wedge (blizzard tight diamond in a blizzard); wall on one
  side → echelon away from it; any passage between two walls → column (narrow sectors / herringbone by width); arrival at a wall → the wall layout. Needs hysteresis (like today's
  `ColumnHoldTime`) so it does not flicker at wall gaps.
- **Menu rules (2026-10-25, stage 7 prep)**: release on an item picks it (holds until Auto), release in the centre = Auto,
  Esc / RMB cancels (the previous choice stays); with one operative every item is greyed out (a pick is refused, Auto works);
  the diamond item is the wedge with 3, the pair with 2; too few members for the held choice -> back to Auto. Icons in
  `Content/UI/FormationMenu/src/*.svg`; contract section 9.
- **Manual: hold G** → radial menu with schematic icons; move the cursor to a sector, release G. Centre = **Auto**.
  A manual choice holds until Auto is picked. Proposed layout (user named 12 line, 3 echelon right, 10 echelon left,
  5–6 column; 9 chosen for symmetry — confirm): 12 Line · 1:30 Diamond/wedge · 3 Echelon right · 4:30 Blizzard ·
  6 Column · 7:30 Hedgehog · 9 Echelon left · 10:30 "All forward" (walk, everyone faces 12).

## Test lab + bots (decided)
- A big maze-like **formation lab map**: open areas, long walls, 90° corners, walls with gaps (wall–gap–wall), narrow
  corridors. Built by a script from a layout JSON, never by hand.
- **Rename `Tools/WaveEditor` → "Training Ground"** with departments: waves bot (existing), movement / formation bot,
  level-design bot, … Rewrite the architecture if needed.
- **Token policy (user):** bot runs must not burn the coding agent's session tokens.
  - Simulation + checks: deterministic TypeScript in Node inside Training Ground (no Unreal, no LLM, 0 tokens) —
    hundreds of routes per run through the same layout JSON.
  - Analysis / advice / parameter search between run series: Jev (`TYPESAFE_API_KEY`, `Scripts/Tools/jev_client.py`)
    or the Claude API (`ANTHROPIC_API_KEY`, like `Scripts/Tools/visual_qa.py`) — the user's API tokens. Without a key
    the bot still runs and writes a plain report.
  - The coding agent reads only the short distilled report (20–30 lines), never raw runs.
  - Parity: the same layout JSON feeds the simulator and the Unreal map; pure C++ rules get parity tests against
    golden vectors exported by the simulator (same inputs → same outputs), like the Godot parity tests.
- Unreal is needed only for the final in-game check (smoke command on the lab map).

## Code map (from this session's reading)
- `Characters/SquadFormation.h` — pure slot math (`ComputeSlotPosition`: even slots left, odd right, two per row;
  column mode; `ShouldSwapSlots`; `ComputeFallbackSlots`; `ComputeGroupTargets`). Config `FSquadFormationConfig`
  (SideOffset 260, BackOffset 280, Column 180 / 150, ColumnHoldTime 0.6, RotationSmoothing 3.5).
- `Characters/FacingRules.h` (Godot lerp_angle turning), `RifleLocomotionRules.h` (8-direction Rifle_2 starts / stops /
  walk: strafing and walking backwards already animate), `AOperativeCharacter::IsFacingCombatTarget` (already moves
  sideways / backwards while facing a target — reuse for the formation look yaw), `SetIdleFacingYaw` (parked followers
  face the leader's way — replace by sector centres on halt).
- `Tactics/GroupCoverRules.h` (+ `ACodexTacticsPlayerController::OrderGroupCover`): slots along the same wall,
  minimal-travel assignment — add side binding (L left, P right, 4th at the roomier end) and the facing-out pose.
- Sight: `docs/systems/sight-and-vision-cones.md`. Squad / orders: `docs/systems/squad-and-orders.md`.

## Suggested order
1. Training Ground shell (rename, departments) + layout JSON schema + movement simulator with the pure formation rules
   in TS + movement bot + report (Jev / Claude API distillation).
2. Formation lab layout JSON + Unreal map generator script + navmesh bake.
3. C++ pure rules (`FormationShapeRules` / `FormationSectorRules` / terrain classifier) with parity tests from golden
   vectors; runtime integration (slots, look yaw, patrol pace, wall layout + pose); hold-G radial menu.
4. In-game smoke on the lab map.
