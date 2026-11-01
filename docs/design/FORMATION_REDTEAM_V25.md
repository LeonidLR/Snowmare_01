# Red team v2.5 - triage (2026-11-01, branch formation/redteam-v25)

Source: the interrupted run of 2026-10-28 (`REDTEAM_v25.md`: 4 rounds, $0.37, 16 layouts, 6 kept, 12 distinct (check, choice, members)
failures). Every failure was reproduced in the deterministic simulator (`npm run bot:movement -- --layouts <dir>`, 0 tokens); **no new paid
red-team run** (spent on the triage: $0). Rule / integration details for the port: `FORMATION_CONTRACT.md` section 16. The user's decisions in
`FORMATION_SECTORS.md` are unchanged; what would change one is a question below.

Classes: **a** = a rule bug (fixed in the TS rules, golden regenerated, regression case); **b** = a layout / outcome the rules handle as decided
(kept as a known entry with a `triage` note); **c** = a check / gate / report bug; **sim** = the simulator differed from the runtime (fixed in
`sim.ts`, the runtime already does it); **Q** = a design question for the user.

| # | failure (layout, check, choice, members) | root cause | class | fix | C++ port? |
|---|---|---|---|---|---|
| 1-3 | 13_4 #3 flicker auto n2 / n3 / n4: "wall -> wedge (diamond), back after 0 s" | the route ends 1 m off the wall, the leader's wall slot 45 cm off it: the arrival test switched to the slot (55 cm > `classify.arrive_dist`) for one frame | sim | the destination = the nearest point of click -> wall slot (contract 16.2) | no - the runtime keeps the click and counts 0.25 s standing as the arrival |
| 4 | 13_4 #14 covered approach auto n4 ("flank 100 %" - really left 40.5 %) | the same one-frame drop re-dealt the approach roles mid-way | sim + c | fixed by 1-3; the failure text now names both flanks | no |
| 5-7 | 13_4 #5 diamond n4 K-L 20 cm; 15_4 #5 auto / diamond / all_forward n4 K-L 68-71 cm; 16_4 #5 auto / diamond / all_forward n4 K-P 39-57 cm | after a 90 deg turn the flank man crossed the leader's own way to his new place; the simulator lacked the runtime's yield (86770ff7) | sim | `yieldFromLeaderWay` = `FormationRuntime::YieldFromLeaderWay` (16.2); also clears 10 older #5 entries (1_7, 7_5, 7_8, 9_1) | no - already in the runtime |
| 8-10 | 16_4 #4 zone auto n2 / n3 / n4: "Z1 wants wall: 0 %" | (1) end-on at a T stem's free tip the rays saw only the long faces - no wall ahead, no wall layout; (2) then 57 %: check 4 counted the walk through the zone (the wall layout is arrival-only) | **a** + c | rays stop at a thick wall's end faces (16.1, golden scene `stem_tip_ahead`); the sim's wall layout lines up across the tip; check 4 judges `wall` only standing (16.3) | **yes - ported** (`formation/port-v251`): `TerrainRaySegment` end faces (`RayHit` default on, `SideClearance` off); `Parity.Classify` 0 diffs; `FormationLabSmoke` `R_G_end` / `R_G_end_axis` |
| 11 | 14_3 #1 wall stare hedgehog n4 s2: "L at L3" (0.30 s = the threshold) | a MANUAL hedgehog in a 4 m zigzag corridor, re-opening after the guard's column: a man jostled there - the over-the-shoulder limit's centre (his raw sim step) flipped +-90 deg frame to frame, ClearLook flipped between two edges, his turn crossed the wall 1.2 m away | b (Q2 = a family: a manual tight shape through bends is the player's choice) | none; known entry with a triage note. Tried: the limit from the smoothed velocity (as the runtime) - fixes it but 5 NEW failures (#7 / #5 on 8_7, 11_8, 12_3: the lagging limit slows the men), rejected | no |
| 12 | 14_1 #3 flicker auto n2 / n3 / n4: "wedge -> echelon_left, back after 4.9 s" | a 60 cm pillar 2.4 m beside the path reads as "a wall on one side": echelon at once (tighter), back to the wedge 5 m after it (8.5 m in all) | **Q1** | none (rules as decided) | - |
| 13 | 15_3 #3 flicker auto n2 / n3 / n4: "echelon_left -> wedge, back after 5.6 s" | a U-turn round a 30 m wall's end: ~15 m of really open ground at the apex -> wedge after 5 m (the user's hold), the echelon again on the far face; one change back on a 94 m route = 1.06 / 100 m | **b** (Q3 optional) | none; known entry with a triage note | - |
| 14 | 14_1 #6 rear pace auto n4 s2: p95 203 cm | Z (rear guard, walking backwards) stepped behind a 1.5 m stub (the echelon's slot swung over it), lost the leader -> trail column; catching up backwards at 0.9 x walk vs the squad's 0.8 = 22 cm/s margin -> ~10 s behind | **Q2** | none (the pace is a user decision) | - |

Count: the run kept 22 `expected_fail` entries on 6 layouts = 12 distinct (check, choice, members) keys. Fixed: 14 entries (13_4 x 5, 15_4 x 3,
16_4 x 6) - every key of #4, #5, #14; the three #3 keys are fixed on 13_4 but stay open on 14_1 / 15_3 (Q1, Q3), #6 stays open (Q2), #1
hedgehog stays as a known manual-formation glance (row 11): 8 entries open, each with a `triage` note in its file.

Also fixed (class c, found on the way): the regression table (`redteam_regression.md`) left out checks 13 / 14 (counted, not shown); the
red-team report's "rule bug?" list was stale (now `RULE_BUG_CHECKS`); the model's brief still described the pre-v2.2 triad ("K P L, the 5-7
gap kept by design") - a wrong brief makes the model write wrong expectations. Not changed, for the coordinator: the red team records check 2
only with 4 men (`runFailures`) while the bot gates it from 3 (v2.2) - aligning them may add triad entries to the kept layouts.

## Questions for the user (game designer)
1. **Q1 - a short obstacle beside the path (14_1).** A 60 cm pillar / stub within 4 m of the path switches the squad into the echelon at once
   and back 5 m after it. Options: (a) leave it (any wall surface beside = "a wall on one side"); (b) the classifier ignores side obstacles
   shorter than ~1.5-2 m (pillars stay ClearLook's job: nobody looks into them); (c) such short obstacles switch only after they have been beside
   the leader for some path (a "tighter" hold). Recommendation: (b).
2. **Q2 - the rear guard falling behind (14_1).** Walking backwards the rear guard catches up at only 22 cm/s more than the squad (0.9 vs 0.8 of
   the walk), so after any detour he trails 2 m for ~10 s. Options: (a) leave it (the 2026-10-25 "Вариант А, потом будем смотреть"); (b) more
   than ~1.5 m behind his place he turns and walks forwards (faces his run) until he is back, then turns round (the rear briefly open); (c) the
   squad slows while he lags; (d) `move.backward_mult` 1.0. Recommendation: (b) - the same "the squad never splits comes first" the approach
   already uses.
3. **Q3 (optional) - a U-turn round a wall's end (15_3).** The squad leaves the echelon at the apex (open ground) and takes it again on the far
   face. Options: (a) leave it (recommended - it is the 5 m hold as decided); (b) hold the echelon through a turn the fold already covers.

## Answers (game designer, 2026-11-01) - rules v2.6 (`FORMATION_SECTORS.md` "v2.6", contract 18)
- **Q1 = b**: obstacles shorter than 1.5 m along the path are no side walls (`classify.side_min_length` 150, `classify.side_join_gap` 50).
  14_1 #3 (auto n2 / n3 / n4) fixed; 14_1 #6 fixed too (no echelon -> Z never steps behind the stub); also 3_8 #3 x 3 and 2_4 #6.
- **Q2 = b**: the rear guard more than 1.5 m behind his slot turns round and catches up facing forwards; back within 0.5 m he walks backwards
  again (`move.rear_turn_lag` 150 / `move.rear_turn_back` 50; no turn start while a team-mate is on his way). Alone (Q1 off) it takes 14_1 n4 s2
  from p95 203 to 88 cm; with Q1 the 14_1 case no longer lags at all.
- **Q3 = keep**: 15_3 #3 stays a known entry (the 5 m hold, as decided).
