# Formation look sectors & tactical movement — design decisions (session 2026-10-25)

Status v2.1 runtime (2026-10-26, branch formation/runtime-v21): **in the game** behind `Codex.Formation.Sectors` + `Codex.Formation.V21` (0 = the
v2.2 runtime without it) - the wall guard (mirror echelon / the trail column along the leader's recorded path, checked 5 m ahead on his path, the
request back after 5 m whole; HUD line "Echelon left -> Column (wall)"), the sharp-turn fold, the bypass side "B" (shortest / shielded by a seen,
heard or reported threat, steered round the wall end), the corner check (slicing the pie, also the leading man of a wall slide), ClearLook in every
formation, places re-assigned on every formation change, fallback slots on the leader's side; the level is read as walls from its box collision.
The menu's choice exists in the runtime (dev console `Codex.Formation.Menu`) and since stage 7 (2026-10-26) the hold-B radial menu sets it (below,
"Selection"). Smokes `FormationLabSmoke` (checks 7 /
9, the fold, manual echelon left / line) and `FormationBypassSmoke`; details `docs/systems/squad-and-orders.md` "Rules v2.1 in the runtime".

Status v2.2 (2026-10-25, branch formation/runtime-v22): **in the game** behind `Codex.Formation.Sectors` - the triad K / P / Z, the coverage allocator's
patrol / halt looks with the rear guard (`Codex.Formation.CoverAllocator 0` = the v2.1 tables), places by minimal travel, the wall group after a group
cover switch / wrap / re-entry (the leader's look fixed, the others close the open arc - the user's playtest bug), the wall slide; smokes
`FormationWallGroupSmoke`, `FormationSlideSmoke`; details `docs/systems/squad-and-orders.md` "Rules v2.2 in the runtime". (The v2.1 corner check and
the guard / trail column followed on 2026-10-26, above.)

Status: **implemented in the game (stage 6, 2026-10-25, branch formation/integration)** behind `Codex.Formation.Sectors` (default 1; 0 = the
old triangle formation): pure rules v2 (`Source/CodexTactics/*/Formation/`), runtime in `USquadSubsystem` (`SquadSubsystemFormation.cpp`), facing in
`AOperativeCharacter`, the group cover order in group mode = the wall layout; smoke `CodexTactics.FormationLabSmoke`; details
`docs/systems/squad-and-orders.md` "Formation look sectors". Not yet (stage 6): the radial menu (manual formations, "All forward"; done in stage 7,
hold B, 2026-10-26), the hedgehog at a
heat source (`HedgehogSlots` exists), the designer's own back-to-the-wall clip (baseline: crouch idle facing out).
Visual reference with interactive schemes and 3-second clips: `docs/design/formation-sectors.html` (open in a browser;
same page as the claude.ai artifact https://claude.ai/artifact/ErDQfTKvYcV1Q3YzaA19HT). Keep the page, the clips and this
file in sync with every design change (user request: "update the animations with every fix").

## Core rule
An operative sees only his vision cone (already in game: 140° standing / crouched, 110° prone; out of cover the cone is
the body facing, `AOperativeCharacter::GetSightFacing`). The formation decides where each one looks. 12 o'clock = the
leader's heading.

## Roles and sectors (decided; the walk / halt sectors of 3-4 replaced by the v2.2 allocator below)
| Role | Slot (from the leader, m) | Walk = patrol | Run (double click) | Short halt |
|---|---|---|---|---|
| Commander (K) | 0, 0 | 12, light search 11–1 | 12 | 12 |
| Right (P) | 2.6 right, 2.8 back | scans 12 → 3, dwell on points | 12 | 2 |
| Left (L) | 2.6 left, 2.8 back | scans 9 → 12, counter-phase | 12 | 10 |
| Rear (Z), 4th member | 5.6 straight back (today slot 2 = 2nd row LEFT — change) | 6, walks BACKWARDS | 12 | 6 |

- 3 men = wedge (triangle; **v2.2: K, P, Z** — see below); 4 men = **diamond** (rear centred). No 2×2 square.
- **Patrol pace: the whole squad walks at ~80 % of walk speed** so the rear man keeps up walking backwards.
- ~~**Triad keeps its rear gap** (5–7 o'clock, ~40°): the honest cost of three men; the 4th closes it.~~ **Replaced 2026-10-25
  (v2.2)**: the triad has a rear man.
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
  *Clarified 2026-10-26 (v2.3, coordinator from the v2.2 runtime review)*: the v2.2 coverage allocator must not override these
  looks - in a narrow column K (12), P and L are fixed looks and the allocator only shares out what is left; Z keeps the rear
  (the rear guard). The wall layout (L 8 / K 6 / P 4) and the pair (3 -> 6 -> 9) never go through the allocator.
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
  walls and passages keep their normal rules; the hold-B menu still overrides.

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
- **Sharp turn = fold (user, 2026-10-25, "5Б")**: before a sharp turn on the path (over 25°, seen 8 m ahead) an echelon or a line
  folds into the column along the leader's path, and opens again 5 m after the turn — the far end no longer swings metres round
  the turn and the rear man keeps up.
- **Nobody looks into a wall, in manual formations too (user, 2026-10-25, "6А")**: the slots stay as chosen, but a man whose
  sector would face a wall closer than ~2.6 m turns his look to the nearest open direction (his scan stays in the open part of
  his sector) — and never sweeps his look across the wall to get there. Only the leader may keep looking where he walks.
- **Places, not fixed slots (coordinator, 2026-10-25)**: on every formation change the men take the new places by the shortest
  total walk (nobody walks through anybody); the sectors belong to the place (the man at the rear place looks at 6).
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
- **Manual: hold B** (user decision 2026-10-26: G is the grenade, so the menu moved from G to B) → radial menu with schematic
  icons; move the cursor to a sector, release B. Centre = **Auto**. A TAP of B (released within 0.25 s,
  `ACodexTacticsPlayerController::FormationMenuHoldSeconds`) is still the solo / squad toggle; the hold opens the menu only in
  squad mode with >= 2 members, in exploration or the tactical pause (never in turn-based combat, never while the tutorial locks
  `Squad.Formation`) - otherwise a hold of B is a plain tap (the toggle on the release). The menu opens at the cursor (pulled in to
  stay on screen); Esc / RMB cancel it; the camera does not edge-pan while it is open; the game is not paused by it. In the game
  since stage 7 (2026-10-26, `UFormationMenuWidget`, smoke `FormationMenuSmoke`); English labels in the game (Auto, Line,
  Diamond / Wedge / Pair, Echelon right, Blizzard diamond, Column, Hedgehog, Echelon left, All forward).
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
   vectors; runtime integration (slots, look yaw, patrol pace, wall layout + pose); hold-B radial menu (stage 7, done 2026-10-26).
4. In-game smoke on the lab map.

## v2.2 — the rear guard and the coverage allocator (user decisions 2026-10-25, via the coordinator; contract section 12)
- **The triad = K, P, Z (user, 2026-10-25, after playing the build: "when we walk as three the rear sector is too open; one of
  them must walk backwards, like the four")**: K leads (search 11–1), P right-behind sweeps BOTH flanks 3 → 12 → 9 → 12 → 3
  (K's search runs against him), Z straight behind (`triad.rear_back` 4.2 m) walks backwards at 6. Halt K 12, P 3, Z 6; run
  everyone 12. Replaces "the triad keeps its rear gap" (above) and the "column of three keeps the gap" note (v1.2). The left end
  of the wall layout looks 8 whatever the role (in the triad: Z).
- **The last man is responsible for the rear — in every formation (user, 2026-10-25, screenshot of an echelon along a wall
  whose last man looked forward-right: "the last man must look the other way, too big a sector opens; recompute all variants")**:
  with 3+ members the place farthest back along the leader's path is the REAR GUARD: he walks backwards and holds 5–7 o'clock,
  turned towards the open side (about 5 o'clock) when a wall stands on one side of him, never into a wall; it re-assigns with
  the places after any formation change.
- **Coverage allocator replaces the per-formation sector tables on the move (coordinator, 2026-10-25)**: K keeps the front,
  the rear guard the rear, the others split the remaining OPEN arc (walls excluded) — the largest uncovered gap first, to the
  nearest man, each sweeping inside his share (narrow enough that its far side stays within 60°); halt = the centres of the
  same shares; run everyone 12; the pair, the wall layout and the hedgehog ring keep their rules.
- **A look decided elsewhere counts as cover (user playtest, 2026-10-25: in group cover at a wall corner the leader held the
  new corner and the two followers looked the same way, a wide gap opened)**: a member whose look is fixed (the leader's corner
  aim, a combat target, an explicit facing order, the corner check) counts as covering his cone; the others fill the rest
  (`FormationCoverRules.AllocateStatic` for a standing group, the fixed looks of `Allocate` on the move).
- Bot: check 2 (rear cover >= 90 %) for every formation of 3+ (manual too); new check 10 — the largest open arc nobody covers
  <= 60° (`coverage.max_gap_deg`) for >= 90 % of patrol. Tunable keys: `rear.share_half`, `rear.wall_shift_deg`,
  `coverage.max_sweep`, `coverage.max_gap_deg`, `triad.rear_back`.
- **Wall slide (user, 2026-10-25, approved on an animation: "Все отлично")**: the squad stands at a wall and the next destination
  lies at the same wall (within 2 m of it, along it): nobody re-forms - everyone moves sideways together along the wall at patrol
  pace, the order and the offset from the wall kept, nobody passes anybody, nobody goes round the wall. The leading end looks ahead
  along the wall (a little outward), K away from the wall (his search), the trailing end back along the wall - he is the rear
  guard. On the stop: the normal wall layout (the 4th stays on his side when there is room - otherwise he would walk through the
  line). A destination farther from the wall: the normal formation. At the wall's end the corner check (the leading end) applies.


## v2.3 — red-team fixes (coordinator task 2026-10-25, overnight; contract section 13; the user's v2.2 decisions unchanged)
- **No looking into slivers, and no look flip-flop (ClearLook, red team #1)**: in zigzag corridors a 5–13° gap between two
  wall spans opened and closed as a man walked and his look swung across a wall 2 m away to reach it. Now a gap narrower than
  30° (`look.min_free_deg`) is no place to look, and a man keeps his previous clear look (60° preference, `look.hold_deg`)
  until a wall comes close to it — no more jumping between two far directions. Boxed in on all sides: he looks along the
  longest clear line instead of into a wall. Refines "6А" (nobody looks into a wall); nothing else changes.
- **The trail column narrows in a tight gap (red team #7)**: crossing a 1.2–2 m opening at an angle, the column's zigzag
  (60 cm aside) put a man inside the wall; now his offset aside shrinks until he keeps 45 cm from the wall (on the leader's
  track at worst). Nothing changes where there is room.
- **The narrow column keeps the user's looks (clarification of 1A, coordinator 2026-10-26)**: in a corridor narrower than 6 m
  the column's K, P and L look as decided in 1A (12, forward over the shoulder, back at a slant) - the coverage allocator no
  longer re-shares them; Z is the rear guard. (`FormationSectorRules.Decided`; the allocator takes them as fixed looks.)
- **A narrow gap without a sweep beats a wide one across a wall (ClearLook, red team #1)**: entering a corridor the right man's
  look (forward over the shoulder) hit the end of the corridor wall; he swung 110° across that wall end to the far side and back.
  Now a narrow free gap he reaches without passing the wall is preferred to a wide one behind the wall; a wide gap without a
  sweep still wins, so the zigzag-corridor slivers stay out.
- **No two men sent to the same spot (red team #5)**: when a formation's places don't fit (a hedgehog of three in a 3 m corridor),
  the stand-in places now keep the normal spacing from each other - two men were sent to the same point and walked into each other.
- **A man cut off brings the column back (red team #7)**: if anyone loses the line to the leader behind a wall, the squad
  folds into the column along the leader's own path at once (it leads him round the corner); it opens again 5 m after everyone
  is back in sight. (The leader turned into a side passage just as a manual blizzard re-opened; its far man was left round the
  corner.)

## v2.3 — the user's answers (2026-10-26 morning, via the coordinator; contract 13.7-13.9)
The five overnight questions below were answered; the question texts stay for the record.
- **Q1 = a (decided 2026-10-26)**: only a corridor the terrain classifier holds counts as the 1A narrow passage - a doorway crossed
  at an angle is shared out by the allocator, which sees the walls. Already what the rules do (contract 13.3).
- **Q2 = a (decided 2026-10-26)**: a manual hedgehog / blizzard / column through a hairpin is left as it is - the player's choice;
  "5Б" folds only the line and the echelons. 12_6's #5 entries are accepted (no rule bug).
- **Q3 = c (decided 2026-10-26, implemented in v2.3 part 7)**: only the rear guard walks backwards. A member who is not the rear
  guard (nor the leader, nor a fixed look) and whose coverage share is the rear looks back over the shoulder - at most
  `look.over_shoulder_max_deg` (110°) off his direction of travel - so he still walks forwards at the squad's pace instead of
  backwards at the backward cap; his wall-avoiding look (ClearLook) stays inside that limit too. The rear guard (Z) keeps walking
  backwards at 6 as decided.
- **Q4 = b (decided 2026-10-26, implemented in v2.3 part 8)**: ClearLook anticipates the man's motion - the walls are also
  seen from where he will be in `look.anticipate_s` (0.5 s), so walking past a wall end within ~1 m he turns his look away
  from where the end will be, instead of an edge that swings round behind him faster than he turns (red team #1: 3_4, 11_8,
  1_4, 8_2, 9_4, 12_3). His held look is still judged from where he is now (else, leaving a corridor, the two wall ends behind
  him closed the gap he looked through and his look swung across the wall beside him).
- **Q5 = a (decided 2026-10-26)**: the pair's rear man keeps his 3 → 6 → 9 sweep at a narrow mouth (the decision for the pair stays);
  those #1 entries are accepted.
- The HUD line of the turn fold says "(turn)", e.g. "Line -> Column (turn)"; the wall guard keeps "(wall)" (also when a wall
  cuts the formation during a turn, and for a man cut off) - v2.3 part 9 (`GuardState.cause`).
- No tuning for the manual line's rear man (its pace, #6 in parentheses, stays not gating).

## Open for the user (v2.3) - answered 2026-10-26 (above)
Picked overnight by the most conservative reading of the recorded decisions (coordinator 2026-10-26: "don't wait"); each can be
flipped in the morning.
1. **What counts as the "narrow passage" of 1A for the trail column?** The trail column measured each man's side room; crossing a
   1.2-2 m doorway at an angle that read "narrow", P got 1A's "forward over the shoulder" and looked straight at the door's wall
   end 0.5 m away (55 new wall stares on the red team). Options: (a) **only a corridor the terrain classifier holds** (the
   passage width) - a doorway crossed at an angle is shared out by the allocator, which sees the walls; (b) any narrow side
   room, as before (more stares at door edges); (c) 1A at a doorway too, but P's +15 turned away from the near wall end.
   **Picked (a)** - 1A was decided for a narrow passage (a corridor), and (a) changes nothing in a real corridor.
2. **A hairpin round a wall end in a manual hedgehog / blizzard / column (red team 12_6, #5)**: the route turns 180° round the end
   of a wall; the squad's places swing round the leader and two men cut across each other (22-79 cm apart). The turn fold ("5Б")
   folds only the line and the echelons into the trail column. Options: (a) **leave it** - a manual formation through a hairpin
   is the player's choice; (b) fold every manual formation of 3+ into the trail column before a turn sharper than ~120°;
   (c) fold only the hedgehog / blizzard (the tight shapes). **Picked (a)** (keeps "5Б" as decided); 12_6's #5 entries stay
   open as a design question.
3. **Who may walk backwards besides the rear guard? (red team #7 at oblique wall gaps: 12_3, 8_7, 10_6, 11_8 - 9 entries)**:
   crossing a gap both flanks are walls, the only open arcs are the front (K) and the rear (the rear guard); the v2.2 allocator
   gives a man with nothing to cover the share "nearest his bearing" - for the 2nd / 3rd men of the column that is the rear,
   so they walk backwards (slower), fall ~2 m behind and their line to the leader cuts the gap's wall end. Options: (a) **leave
   it** (v2.2 as approved); (b) on the move such a man doubles the FRONT instead (tested: fixes 11, but 31 new wall stares - the
   forward look hits the gap's wall); (c) he looks back over the shoulder, at most ~110° off the heading, so he still walks
   forwards. **Picked (a)** - (b) is worse on the red team, (c) changes the approved allocator; for the morning.
4. **Passing a wall end within ~1 m (red team #1, ~10 entries: 3_4, 11_8, 1_4, 8_2, 9_4 at a corridor entrance; 12_3 at a gap)**:
   the end's direction swings fast as he walks by; ClearLook keeps his look 55° off it on the side reachable without passing it,
   and that edge swings round behind him faster than he turns - for 0.3 s he is within the check's 45°. Options: (a) **leave**;
   (b) ClearLook anticipates the man's motion (blocked spans also from his position ~0.5 s ahead - a contract change for C++);
   (c) the check ignores a wall END he passes within 1 m (only faces count). **Picked (a)** for tonight; (b) recommended for
   the morning (rule work, not a decision change).
5. **The pair's rear man leaving a narrow mouth (red team #1, ~10 entries: 9_3 x5, 2_8, 2_4, 12_7, 1_4, 5_7, 5_8, 8_2)**: his
   3 → 6 → 9 sweep (user decision for the pair) crosses the mouth's wall ends right beside him. Options: (a) **leave** (the sweep as
   decided); (b) the pair's rear man holds 6 while a wall end is within ~1.5 m. **Picked (a)** (keeps the decision).

### Remaining after v2.3 (101 + 1 red-team entries; triage)
- #5 spacing (34): mostly 69-80 cm for 0.3 s in tight manual shapes (hedgehog / blizzard of 3) at turns and hairpins (question 2),
  and two men overtaking in a column after a re-assignment; no single rule bug found tonight.
- #3 flicker (12: 1_7, 2_7, 3_8, 7_8, auto n2-4): the terrain class flips X -> Y -> X within 3-5 s; pre-v2.2, not investigated yet
  (classify.hold_distance 500 vs the red team's short zones).
- #4 (3: 1_1) and #8 (3: 2_6): the red team's own expectations (zone formation, bypass side) - not investigated; may be layout
  expectations rather than rule bugs.
- #6 rear pace (5), #2 rear cover (2: 88-89 % vs 90 %): borderline.

## v2.4 - the squad at a wall's end (user bug report 2026-10-26, game designer testing rules v2.3 in the editor; branch formation/v24-corner-peek)
Runtime fixes (no change to the TS reference rules / golden; pure glue `Formation/FormationWallEndRules`, tests `CodexTactics.Formation.WallEnd.*`;
smokes `FormationCornerPeekSmoke`, `FormationWallSwitchSmoke`, `FormationSquadPathPreviewSmoke`, `FormationLeaderSwapSlideSmoke`). The Training
Ground simulator does not model the corner stack / the fitted layout yet (follow-up for the simulator session).
- **A - the corner stack (user, 2026-10-26: "the man on the corner side should come up and peek round the corner; the Commander stays behind
  him; the man on the far side looks back / left along the wall, covering the rear")**: the squad stands in the wall layout and the leader's
  next destination lies at a FREE end of the same wall (from 1.25 m before the end to 2 m past it, on the squad's side, within
  slide.max_off_wall of the wall; a joint - an L / T corner - is no free end) -> the wall slide runs to the edge: the place nearest the end (the
  point man) stops 40 cm short of it and looks round the corner (the corner watch's look at the corner: 50 cm beyond the wall body), the
  others keep the slide's order behind him (with three, K in the middle = second), the trailing end looks back along the wall turned 20 deg
  out (the rear guard), K away from the wall; held, crouched after the group cover order, until the leader's next order. The cause: a
  destination at / just past the end lay outside the measured wall, so the order was "no slide": the wall order ended, the squad walked a
  normal column / echelon and the leader himself stood at the edge looking along the wall (the corner watch only runs on the move); a click
  just inside the end slid the line on and re-formed the plain layout (L 8 / K 6 / P 4) - nobody peeked. Interpretation: "K in the middle"
  of the slide holds - in a stack of three K behind the point man IS the middle; with four the slide's order is kept (nobody passes anybody),
  so K may be third (a fourth man between them looks out, turned to the rear by wall.neighbour_deg). A click clearly round the corner (behind the wall's face line) stays a normal move (the squad goes round).
- **B - the wall layout never splits the squad (user: "the team split completely, which must never happen")**: FormationWallRules::Layout
  put the members a step either side of K whatever stood there - at a T / L junction or the wall's end a place landed inside or behind the
  other wall (an unwalkable place: a stand-in round the leader's CURRENT position while he still ran round). Now every layout (the group
  cover order, the automatic one, the one held after a slide or a leader switch) is fitted to the world: a place cut off from K's place by a
  wall body, inside a wall's clearance or off the nav mesh moves to the next free step on K's other side; the moved line's looks follow the
  positions (outer end wall.end_deg, its neighbour wall.neighbour_deg, alone wall.flank_deg; the pair +-45 mirrored).
- **C - the squad's guide lines (user: the dashed line along / round the wall shows for one operative, not for the squad)**: the single
  operative's wrap preview needs him in a cover slot; the squad in the formation wall layout stands in none. Now, standing in the wall layout
  in group mode, the ground under the cursor along the wall or round its end (within slide.max_off_wall of it) shows the same dashed lines:
  a slide (and the corner stack) - each member's line to his place; else the leader's nav-mesh path (it wraps the wall's end).
- **D - one leader switch at the wall broke the next move (user: "change the leader ONCE: the members swap places, as they should; after
  that the slide along the wall no longer works")**: (1) the switch itself counted as a new order of the new leader (his move serial was
  compared with the old leader's): a phantom slide / bypass to his stale formation-follow destination - often a team-mate's spot, so he
  pushed against him at ~3 cm/s, "walking" in place; (2) the switch ended the wall order ("the leader changed"): the squad lost the hug
  (no crouch), re-formed in the automatic layout. Now a switch is no order, and the wall order (a running slide too) goes on with the new
  leader: the layout round him at the same wall (K in the middle), crouched as before, the places by minimal travel (a man re-forming past a team-mate standing in the
  line steps round him on the open side - the nav mesh ignores bodies); his next click slides the line as with the original leader.

## v2.4 — the wall end's open arc (user bug report 2026-10-26, via the coordinator; contract section 14; branch formation/v24-wall-end)
- **With the corner stack above (coordinator 2026-10-26)**: the stack's looks are fixed (the point man round the corner, K out, the trailing
  end back along the wall); the open-arc widening below applies only AROUND them (a fourth man between them may be spread) - `EndLooks`' `fixed` roles.
- **2026-10-26: wall end: the group covers the whole open arc, flanks included (user bug report)** - in the editor
  (`Codex.Formation.Debug`) three operatives crouched at the END FACE of a thin free-standing wall left large uncovered zones on both
  sides of the end, along the wall's two faces ("big uncovered zones form on the sides of the end - stabilise it so they cover the large
  zones"). Cause: the wall layout's looks (L 8 / K 6 / P 4) are the rule for a wall FACE, where the open side is a half-plane; at the end
  face the open arc reaches round the end, back along both faces, and those looks (+-130 deg from the normal together) leave both flanks
  open - ~69 % of the open arc held with 3 men, ~71 % with 4.
- **The rule (v2.4, `FormationWallRules.EndLooks`)**: the layout's places and K's look (6 o'clock, away from the wall) stay; when the layout's
  looks hold less than `wall_end.cover_pct` (95 %) of the open ring round the group (`wall_end.probe_dist`, 6 m: the points somebody has a
  clear line to), the others' looks are spread over what is left - greedily, each the look that holds the most still-open points he can
  see, the least turn from his layout look on a tie, nobody looking into a wall. At the end face of a thin wall: K out along the wall's
  axis, the two men beside the end ~118 deg off it, back along "their" face (3 men: 69 -> 100 %; 4 men: the 4th keeps his end look).
  A face (the half-plane) is held already: nothing changes there - the user's wall rule stands. Computed once per layout from the
  slots: no flicker, no reshuffle while they hold (`Codex.Formation.WallEnd 0` = the face looks everywhere).
- Interpretations (coordinator / agent, for the user): the pair keeps its K 7:30 / partner 4:30 at an end too (a user decision; two cones
  cannot hold the ~330 deg there); a face whose end man stands PAST the wall's end is spread the same way (he looks round the end).

## v2.5 — the covered approach to the wall (user rule 2026-10-26, via the coordinator; contract section 15)
- **The user's rule (2026-10-26, screenshots `to_wall_1/2`)**: the squad moves calmly (patrol, sectors held), then gets the order to hug a
  wall ("Formation: Wall"). Before, everyone dropped his sector and dashed to the wall, the rear wide open. Now, until they are settled at
  the wall (3 men): (1) at least one keeps covering the withdrawal - the rear, the way they came / away from the wall - the rear guard,
  walking backwards as approved; (2) the second looks to the side (a flank); (3) the third runs to the wall and looks parallel to it, the
  other way along the wall. As each one arrives he takes his wall-layout look; the rear cover holds until the others are at the wall, then
  he joins. The pair: one runs + looks along the wall, the other covers the rear. Four: the rear, both flanks, the first at the wall
  looking along it.
- **The rule (`FormationWallRules.Approach`)**: roles assigned once when the approach begins and held (no reshuffle): the runner = the
  leader; the rear cover = the follower whose look is nearest the rear (the patrol's rear guard; ties the farthest back); the others are
  flank men (one: his side of the leader along the wall; two: one each side). Rear = the wall's outward normal; a flank = along the wall
  line (normal +-90); the runner at his slot looks along the wall the other way from the first flank man. A member is "at the wall"
  within `approach.arrive_dist` (100 cm) of his slot; when everyone is, the plain wall layout. Every approach look goes through ClearLook.
- **Interpretations for the user (please confirm or flip)**:
  - *Who runs first*: the LEADER - the order is his and he runs to the clicked slot (the alternative: the man nearest the wall).
  - *The rear*: "away from the wall" (the wall's outward normal), which is the way they came on a straight approach; along a wall they came
    from the side - the rear is still "away from the wall".
  - *The rear cover*: the man already looking back (the patrol's rear guard), so there is no gap while somebody turns round; nobody looking
    back -> the man farthest from the wall. He keeps the rear until everyone else is at the wall AND he is at his slot.
  - *Pace*: the leader runs (the order's run); the rear cover walks backwards at the backward cap of the walk (`move.backward_mult` 0.9), the
    flank men walk sideways at the walk pace - neither copies the leader's sprint (a sprinting man faces his run). So the squad settles a
    little later than before (the game smoke: ~4 s instead of ~2 s from 9 m), the price of the covered rear.
  - *The first arrival*: the runner looks along the wall from his arrival until everyone is in, then turns to his layout look (K 6 o'clock).
  - *The squad never splits comes first* (found by FormationWallSwitchSmoke after the merge with the corner stack): a man whose straight way
    to his slot passes a wall (a switch round a corner to a cross wall) goes round it facing his run at the squad's pace - holding his look he
    fell 4 s behind the leader round the corner - and takes his approach look again once the way is clear.
