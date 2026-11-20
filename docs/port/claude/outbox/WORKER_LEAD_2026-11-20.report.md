# Worker stand-in lead, 2026-11-19 evening / 11-20 (report, kept current)

Machine: codex-worker, checkout C:\Work\CodexTactics-slot1. Brief: docs/port/WORKER_LEAD_2026-11-20.md. Branches: worker/lead-2026-11-20
(this report), worker/t05-firing-positions (T-05 continued from origin/claude/firing-positions a1dae05a).

## READ FIRST when the processes move back to the main PC (designer asked for this warning)
- **The stale worker_sync marker was REMOVED** (designer's OK, 11-19 18:21): `C:\Work\CodexTactics\Saved\Worker\SYNC.DESKTOP-6RLUUV8-2172-20261119-170926`
  (main-PC worker_sync from 17:09 for T-05 a1dae05a; that sync had died - no sync / ssh process on the worker). It made the night loop
  wait 30 min before every job. Note left next to it: `Saved\Worker\NOTE_stale_sync_removed_2026-11-19.txt`. Nothing of that sync is
  lost (a1dae05a is on origin). **The main lead must not assume a main-PC worker_sync is still running on the worker; start a fresh one.**
- slot1 had NO untracked marketplace content (1502 of 5520 Content files; every engine run crashed in
  `ULadderClimbComponent::ExtractTrack`, Skeleton == nullptr). Copied the 4018 missing untracked files (11 GB) from
  C:\Work\CodexTactics\Content (robocopy /XC /XN /XO, nothing overwritten, 0 failed). slot2 has the same gap. They show as `??`
  in slot1's git status: never `git add -A` there.

## Day-loop numbers (30 seeds, scripted opponent, this machine; medians)
| run | ref | on_pos | idle_max s | crossfire deg | gap s |
|---|---|---|---|---|---|
| crossroads night c1 / c2 (baseline) | main 19a90f52 | 0 / 0 | 19.4 / 17.9 | 13.6 / 8.8 | 2.24 / 1.93 |
| alley night c1 / c2 (baseline) | main 19a90f52 | 0 / 0 | 14.1 / 17.1 | 17.4 / 17.7 | 1.63 / 1.92 |
| crossroads T-05 iter 1 | c4f8f561 | 0.33 | 4.1 | 19.0 | 2.05 |
| alley T-05 iter 1 | c4f8f561 | 0 | 9.4 | 15.3 | 2.42 |
| crossroads T-05 kill zone 0.25 | 4588fbbc | 0.33 | 3.1 | 22.8 | 1.85 |
| alley T-05 kill zone 0.25 | 4588fbbc | 0 | 8.6 | 18.9 | 2.33 |
| crossroads T-05 iter 3 (reach before contact) | 5544431a | 0.59 | 0.9 | 31.1 | 2.0 |
| alley T-05 iter 3 | 5544431a | 0.33 | 1.4 | 27.0 | 4.84 |
Folders: C:/Work/CodexTactics/Saved/Worker/nightloop/2026-11-19/digest.log; slot1 Saved/Logs/SeedBatch/dayloop-<ref>-{cross,alley}.
Target (brief): on_pos >= 0.67, crossfire >= 45, idle <= 2 s on both. NOT met yet.
My 30-seed runs take the machine with a day-caller marker `SYNC.worker-lead-slot1-*` (removed after each batch), so they never run in parallel with the night loop.

## T-05 phase b
- Diagnosis (seeds 1-2 each, Saved/Logs/SeedBatch/t05-diag-2-*): 0-1 of 3 shooters seated. Causes: (1) candidate starvation - an area
  gather returns 0-3 valid positions from 30-143 scan points; (2) tier 2 (1.8x band) finds FEWER than tier 1 (global stride thins a wider
  ring; slots counted before the ring filter); (3) "forward of the kill zone" (distance to the approach point) drops flank positions;
  (4) phase-b literals in C++ instead of JSON.
- Iteration 1 (Sonnet, 1ad3f83b + c4f8f561): one shared cover scan per area (tier 2 = superset), axis-based forward rule
  (forward_margin_cm 800), all phase-b numbers in JSON, 10/10 FiringPosition + 70/70 Pmc unit tests, 7 gate smokes PASS.
  Correction of cause (1): scan density was NOT the starvation; 40-50 % of slots are high walls without an edge, 30-45 % see no sample.
- Lead finding after iteration 1 (Saved/Scripts/first_shot_geometry.py): the smoke's crossfire is the pair angle AT THE ATTACKERS
  at the round's first shot; the attackers fire first in 4/5 seeds from 25-62 m out, while phase b paired the defenders around the
  OBJECTIVE. Decision: the ground kill zone moves onto the attackers' nav path, kill_zone_reach_factor x reach before the objective.
- Iteration 2 (Sonnet) running: that kill zone + factor sweep on 10 seeds + why seated men are not on position at contact.

## T-05 state (worker/t05-firing-positions 5544431a, gated, merged into this branch 2d2ad1e9; NOT accepted as done)
Iterations on the worker: (1) one shared cover scan per area, axis forward rule, numbers in JSON; (2) ground kill zone on the attackers'
nav path (kill_zone_reach_factor 0.25 after a 10-seed sweep); (3) seat only positions reachable before contact (defender sprint 725 cm/s
vs the scripted attackers' walk 220 cm/s, both from the movement rules; attacker path until within the squad's median reach).
Gate on 5544431a: build OK, FiringPosition 12/12, Pmc 70/70, FiringPositionSmoke / TacticalPmc / PmcArenaTactical / PmcFireLine /
HitchProbe / PmcRoofDefence / PmcStalemate PASS. Idle <= 2 s met on both; on_pos 0.59 / 0.33 and crossfire 31 / 27 short.
**Main lead must review - deviations from the brief (stand-in lead's calls):**
- `min_covered_share` 0.34 -> 0.22 (a brief number): a position must see 2 of 9 area samples, not 3. Alley seats 17 of 30 instead of 4 of 30.
  Same real-shot traces, nothing through geometry.
- `forward_margin_cm` 800 -> 4000: the "not forward of the kill zone" rule is in effect off; only the reach-before-contact rule guards it.
- `attacker_speed_factor` 1.0 assumes the scripted (walking) opponent; vs a sprinting opponent (factor 3.3) fewer men seat - re-measure
  when the opponent changes.
**What limits the rest (evidence in the T-05 report):** the attackers fire first from ~60 m in some seeds (RPG at t 0.6-1.6 s); a seated man
is switched off his slot by the commander's utility (Suppress / Ambush / TakeCover) and the arena counts only bInCover or a tactical post -
that is the squad layer (T-08); the alley's approach has little ground cover that sees it (height / mobile anvil = T-08b).
Proposal for the main lead: accept T-05 phase b as the positions layer (idle met, on_pos / crossfire much better), move the remaining
on_pos / crossfire acceptance to T-06 / T-08 where the commander and fire discipline live.

## Fable review 2026-11-19 ~23:00 (designer asked for it) - CORRECTS the T-05 claims above
Read-only review of the reports, diffs and raw seed data. Lead verified finding 1 himself (winner column of the summaries).
1. **Defenders lose MORE on Crossroads with T-05**: defender (side A) wins per 30 seeds - main night cycles c1-c5: 7*, 15, 16, 19, 14
   (*c1 had unfinished rows); T-05 day loops c4f8f561 / f025 / 5544431a: 13, 13, 11. Proxy metrics up, fight result down. Candidate cause
   (UNVERIFIED): RefreshDefence re-runs AssignFiringPositions on every death and re-pairs / re-walks men mid-fight.
2. Baseline and T-05 ran on DIFFERENT seeds (night loop 1030-1179 vs 1-30); the report quoted only cycles c1 / c2.
3. Tuned on the test set: all variants were picked on seeds 1-10, part of the 30 acceptance seeds. Held-out seeds 11-30: Crossroads
   crossfire iteration 2 -> 3 is 22.8 -> 23.1 (no gain); the 31 deg median is a tuning artefact. Alley's gain holds on held-out seeds
   (on_pos 0 -> 0.67, idle 4.5 -> 0.8 s, crossfire 18.9 -> 37.1).
4. "Idle <= 2 s met" rests on one 30-seed run each; bootstrap 95 % of the median 0.5-3.6 s / 0.5-4.3 s. Three changes in one commit.
5. Metric-shaped logic: ground window stands excluded because the arena counts only cover; attacker_speed_factor fits the walking
   scripted opponent only. forward_margin_cm 4000 has no held-out support (2000 / 0.22 had better on_pos) - revert candidate.
6. Cover scan 32-109 ms on the game thread ~1.7x per round (the first, before SetDefenceApproach, is wasted); up to 16 path queries per
   man per refresh; a JSON comment promises a contact test of "position or start" but the code tests the stand point only.
7. The roadmap's own T-05 proof (overwatch_kills > 0 on X4) is not reported - it depends on the generator's firing gaps.
8. T-021 (low): draught lerps to 1.0, not to the outdoor multiplier; a campfire is "sheltered" in any generated building, even a
   fully leaking one.
9. Priority: ~4 h on T-05 tuning while the generator (designer's first priority) had no commit 17:35-22:13.
**Verdict on my claims:** crossfire is measured at the attackers at the first shot - holds; "on_pos limited by walk time + commander
switching" - mostly does NOT hold (37 of 90 men never seated; in 19/30 seeds only 2 shooters eligible because the guard keeps his G008
post, capping on_pos at 0.67; ~27 of 89 off-position men stand still in Ambush / Hold outside cover); Alley lacks seeing cover - holds
with a caveat; min_covered_share 0.22 - supported, forward_margin 4000 - not; "accept T-05 as the positions layer" - NOT yet.
**T-05 status now: NOT ready for merge into main.** Process changes adopted: stop tuning; generator first; one paired held-out A/B
(main vs T-05 on the night loop's seeds, twice) when the machine is free; tune on 1-10, accept held-out; one change per run; reports
always show win rate, seed sets, all baseline cycles and intervals; brief deviations as questions before a merge.

## Building generator - parapet FIRING GAPS done (worker/bgen-firing-gaps 24db207e / ad5ee46f / 4daabfce; gate ref worker/bgen-gate)
Designer order 11-19 evening: generator + smart enemies first, Training Ground after (T-021 was taken out of that order - my mistake).
- Planner `firingGaps.ts` (159 gaps on 22 overwatch roofs of X4), analysis v2 (overwatch spots on the rim, outward only; new
  `rim_corner_clear_m` 1.65 so spots keep off the corner posts: 40 of 44 spots link to a gap, 4 stand at a ladder top), tactical export
  gap ids, builder spans + 60 cm sills (tags parapet / firing_gap / CodexMantleEdge), `levelgen:firing-gaps` script, docs section 8.
- Tests: TS 640 -> 650, typecheck clean; Python builder 53 -> 59; engine (gate ref) TacticalMap 11/11, AI 123/123.
- X4 refreshed ONLY on the bgen branches (accepted layout + tactical file); L_Gen_Depot_X4.umap rebuilt only on worker/bgen-gate
  (throwaway = bgen + worker/t05-firing-positions). main's X4 files are the T-05 / T-06 baseline and are untouched.
- Gate (worker/bgen-gate): build, map build, FiringPositionSmoke / TacticalPmcSmoke / LevelGenRouteSmoke / TacticalMapSmoke /
  PmcArenaTacticalSmoke PASS. Kneeling (sniper) roof-edge positions over the 5 areas: 0 (T-05 report) -> 7; crouched real shots
  over the sill `SHOT: ok`. Still 26 of 33 sniper-tried spots blocked: a spot sees only through its own 90 cm gap.
- The gate needed a minimal C++ change on the T-05 side (2ed6ee5d, gate ref only): TacticalMapRules reads the spot's `gap` +
  `firing_gaps`; GatherRoofs stands a gap spot at the gap's stand point. **Port it when T-05 and bgen meet in main.**
- Merge note: tacticalMap.test.ts pins the committed X4 tactical file to the layout analysis, so the analysis change cannot reach
  main without the X4 refresh.
**Now:** S2.3 block archetype (Sonnet, TS only, worktree C:/Work/CodexTactics-slot1/Saved/wt-bgen so it never switches slot1);
paired A/B of T-05 5544431a vs main on the night loop's own seeds 1060-1209 (150 per scenario) running in slot1.

## DESIGNER DECISIONS 2026-11-20 night - objective defence (binding for T-05 / T-08)
1. «В обороне любых участков есть только одна основная задача - уничтожение группы противника. Все остальное - это то, как это сделать.
   Удержание точки, или зоны, невозможно, если в твоей зоне есть солдаты противника которые сопротивляются.» -> defence AI is judged first
   by attackers killed per round / the attacker group destroyed (GDD A-00A criterion 5); positions / crossfire / idle are the "how".
2. «Видеть точку - не значит захватить ее.» -> holding = physically inside the zone, alive; no "sees the point" counting; the arena rule
   (attackers hold only with no live defender inside) stays; the zone is NOT enlarged to fit firing positions.
3. Scheme: a covered HOLDER physically in the zone + HAMMERS destroy the group on the approach + FALL BACK into the zone. Fall-back rules
   approved («Давай»): (1) holder down / zone emptying with attackers near; (2) ambush bypassed; (4) not in time for the capture; never while
   >= 2 of 3 attackers are down, not before the opening fire; bounding, never across the kill zone, to a covered post inside the zone.
   Rule (3) "position discovered" = next step. Implementation: worker/t08-holder-fallback (Sonnet, running).
Process: studio discussion (5 Sonnet specialists: game / level / AI / QA / UX) + Fable head-of-studio arbitration -> verdict "keep the
point, fix the roles (T-08)"; Fable found OQ-22 (b) already allows the goal to be empty when bypassed with a fall-back (PL-12), and that
A-00A scores lethality, not holding. Garrison experiment (positions that see the point, <= 0.5 x range) FAILED its pre-set gate:
crossroads wins 53 / 143 (main 82), captures 46 (17), attackers killed per round 1.46 (1.99); alley 16 / 38 / 0.63 (main 16 / 40 / 0.67);
code ships inert (4398591c). Dead arena metrics: owKills and postMoves are 0 in all 750 rows - fix before the T-10 gate relies on them.
4. «Нет, не засчитывается. Только после зачистки зоны от противника.» -> a generated-map mission goal is NOT reached while enemies are alive
   in its zone (today UMissionGoalsSubsystem counts it when an operative steps within 3 m) - a game task (mission goals + room / yard zones).
5. «Да» -> a defended goal is shown as a ZONE on the HUD and the minimap (UX: MINIMAP.md has no zone concept yet) - a game / UI task.

## T-05 paired A/B (the night loop's own seeds 1060-1209, main 19a90f52 = the merge-base) + the cause
Defender (side A) wins, paired seeds: Crossroads main 82 / 143 -> T-05 5544431a 32 / 143 (discordant 58 vs 8), attacker objective
captures 17 -> 86; Alley main 16 / 133 -> 21 / 133, captures 40 -> 76. 150-seed medians of 5544431a: Crossroads on_pos 0.67 / idle 1.8 /
crossfire 29.9; Alley 0.33 / 1.9 / 36.3.
Cause (Sonnet diagnosis from the track files, lead-checked on the win counts): the firing positions take EVERY defender off the point
(median 24 m from the objective, 97 % outside the 6 m radius); the objective clock runs while no defender is inside, so the attackers win
by capture (no defender on the point when the attackers first arrive: 72 of 112 T-05 rounds vs 4 of 112 on main). The brief said only the
hammers take positions and the others keep their G008 role; the code seated every sector shooter. Mid-fight re-assignment on each death
is real (survivors move 11 m in 5 s vs 5 m) but not the main cause.
Fix c3cc6f3e (`keep_occupier`, default true): on a ground objective the shooter whose G008 post is nearest the point keeps it.
Paired A/B of c3cc6f3e (same seeds): crossroads defender wins 44 / 143 (main 82, 5544431a 32), captures 69 (main 17), idle_max p50 10.7 s;
alley wins 20 / 133 (main 16), captures 53 (main 40). Diagnosis of c3cc6f3e (track files): the kept occupier sits at his post 4 m from the
point in the open (98 % of samples not in cover), dies at a median 4 m to the three attackers walking in together while the two hammers are
~21 m away; lethality fell (attackers killed per round main 1.99 / c3cc6f3e 1.35).
**Decision (stand-in lead, 7a0af436): `defence_use` = false by default** (JSON + C++ default): objective defence behaves as on main; the
positions layer, its tests and FiringPositionSmoke stay behind the flag; T-08 decides who holds the point. Gate of 7a0af436: build OK,
FiringPosition 12/12, Pmc 70/70, 6 smokes PASS; PmcStalemateSmoke FAILED twice under -Jobs 3 + night loop load (decision churn 20.25 > 20,
same order re-decided 12 / 9 > 6) and PASSES 4 / 4 alone (churn 9-13) - a load-sensitive smoke, worth a look by the main lead.
**Question to the designer (sent 11-20 night):** (1) "garrison" positions (must see the point, <= 0.5 x weapon range) as an overnight
paired experiment, switched on only if defender wins >= ~70 / 143; (2) keep it off until T-08; (3) approach ambush (DS-00A) with a fall-back
rule = T-08. Lead recommends (1) as an experiment with the default staying off.

## T-08 first slice - covered holder in the zone + hammers' fall-back (worker/t08-holder-fallback efd65621; NOT merged) - FAIL, ships OFF

Built to the designer's 11-20 rules: holder on the best covered post inside the 6 m zone (route never across the kill zone), hammers fire from T-05 positions and fall back by bounding to covered zone posts on rule 1 (zone emptying), 2 (ambush bypassed, only after the first defender shot) or 4 (not in time for the capture); no fall-back while >= 2 attackers are down. New pure rules `PmcDefenceRolesRules` + 5 tests; JSON block `defence_roles` (`enabled: false`). Tests: FiringPosition 13/13, Pmc 75/75. Gate smokes pass ON and OFF (PmcRoofDefenceSmoke flaked once OFF, passed on re-run).

Paired run, seeds 1060-1209 vs main 19a90f52, ONE run, thresholds set before the run (lead re-checked with paired_wins.py):

| variant | def wins | att captures | att killed / round | share >= 2 down | paired main-only / new-only |
|---|---|---|---|---|---|
| crossroads main | 82/143 | 17 | 1.99 | 0.629 | - |
| crossroads garrison (T-05 b) | 53 | 46 | 1.46 | 0.420 | 35 / 6 |
| crossroads T-08 | 51 | 54 | 1.51 | 0.469 | 38 / 7 |
| alley main | 16/133 | 40 | 0.67 | 0.188 | - |
| alley T-08 | 19 | 22 | 0.71 | 0.195 | 11 / 14 |

Verdict: FAIL (crossroads misses all three: kills 1.51 < 1.9, wins 51 < 75, share 0.469 < 0.629). Alley passes and attacker captures drop 40 -> 22.

Cause: the crossroads 6 m zone has NO covered post in 80 of 150 rounds, so the holder stays in the open and most hammers get no fall-back plan - the layer barely runs there and the result equals T-05 keep_occupier. On the alley (cover in the zone) it works as designed. Rule 4 fires alone in ~2/3 of fall-backs, i.e. just when the attackers enter the kill zone - it pulls hammers off positions at the moment they should fire.

Open for the designer: (1) the crossroads test point has no cover - give the zone cover (bigger test zone / generator places cover at goals) or allow an open holder post; (2) should rule 4 wait until the hammers have opened fire? (3) rule 3 + leaving the zone not built. Trainer trap: `defence_roles` numbers would become trainer genes unless frozen in pmc_commander.bounds.json (not touched).

## Goal counts only after its zone is cleared + goals drawn as zones (worker/goal-zone-clear 6f9b16c7, from main 19a90f52; NOT merged) - DONE, gated

Designer answers 1 + 2 (11-20). Generated-map reach goals (`Content/Data/LevelGen/objectives/*.objectives.json`) no longer count while a live "Enemy"-tagged actor (monster or hostile PMC, seen or unseen, wounded or knocked down) stands in the goal zone (same zone test as the squad: room polygon or `radius_cm` circle, same floor). HUD: `GOALS: n/N - CLEAR THE ZONE`, zone outline on the ground (orange, red + "ENEMY IN ZONE" when blocked), outlines on the corner + full minimap, one HQ radio line per contest. Fixed on the way: Parse reset floor_tolerance_cm when label_world_size_cm was absent. Tests: MissionGoals 11/11 (6 new), LevelGen 97/97, UI.Minimap 13/13; MissionGoalsSmoke PASS (stand-in enemy blocks GOAL_1 3 s, then it counts), MinimapSmoke / MissionSmoke / GateExitLakeSmoke PASS. No viewport screenshot (nullrhi) - designer to look in PIE.

Open for the designer: (1) zone = reach radius 3 m - want a separate larger `zone_radius_cm`? (2) red zone tells the player a hidden enemy is inside - OK? (3) critically wounded enemy keeps blocking - OK? (4) enemies on another floor above a street goal do not block - OK?

## S2.3 block archetype (worker/bgen-firing-gaps 0ada99f1; TS + Python mirror, no engine)
Sections / landings / flats / lobbies / slots, story 4 by flat column, `dead_end` tags; flat doors closed (designer), balconies only with
fire escapes (designer). TS 650 -> 664, Python 60; plans-off golden byte-identical; property: 864 blocks planned, 0 validator issues, 0 %
fallback. Review gate for the designer: docs/design/levelgen/S2_3_PLANS.md (ASCII plans, questions B1-B5) - sent to his phone; S2.4 waits.
**Engine check (throwaway origin/worker/bgen-s23-engine 4ab3effb):** planned gen_bgen_test_x6 (generator test map, not the designer's
Depot X6: 52 buildings, 15 planned) + a fixture map L_Gen_BgenPlans (36 m blocks B_x / B_y with two sections, 18 / 20 m blocks, offices with a
1000-1200 core bay). PASS: flat doors -> closed leaf actors (45 + 17 on the fixture, 106 on X6), two cores per 36 m block, full-depth section
wall, balcony slot windows, LevelGenBuildingsSmoke 9/9 and 41/41 (every entry -> room / landing / roof path complete, incl. the 1100-1200
core offices), 1 navmesh island per planned building (new LevelGenIslandsProbe), Bot.Route tests 8/8 incl. ParseMantle.
FAIL: LevelGenRampSmoke (fixture 18/25, X6 27/49 - planned block stair_0 legs time out with a complete path), LevelGenRouteSmoke (X6 8/25,
same as the 11-18 note: bot / movement regression), Ladders X6 44/46 (known rotated gallery).
Defects: (1) builder crash - UE 5.8 Python has no set_can_ever_affect_navigation, no /2 map with a closed door leaf could be saved: FIXED,
ported to worker/bgen-firing-gaps d48f165b; (2) door leaves lose pawn IGNORE after the bake re-save (pawn BLOCK) - leading suspect for the
stair timeouts: diagnosis + fix running (Sonnet, throwaway worker/bgen-door-leaf); (3) planned office with a west entry (B_o12w) STUCK;
(4) 10.2 x 9.4 m 2-storey office falls back to BSP (depth < 1000, probably intended).

## S2.7 plan overlay in Training Ground (worker/bgen-firing-gaps 0ecc50f7; TS only)
Layer «Планировки» per storey (rooms by unit / zone, doors by role, loops, dead-end flats, validator issues on the building; legend =
S2_3_PLANS.md), «Другой план» per building (next plan-seed counter, only that building + its stories re-run, locked refused, one undo step),
map summary (planned / fallback with reasons / legacy, per archetype, rooms, dead-end flats, issues, stories) + the same `plans:` line in the
CLI `--plans`. npm test 664 -> 684, typecheck clean. Headless check on the plans-check layout: docs/design/levelgen/S2_7/*.jpg (4 sent to the
designer). Content/Data: one `plans` row in layers.json. Not done: triad flags (no validator rule), clicking the overlay, a re-planned building
built in the engine. Also on this branch tonight: builder fixes d48f165b (nav flag) + d9e78dfd (door leaves stay walk-through after the bake).

## T-022 Training Ground map edit + F11 (worker/t022-tg-map-edit 7be7d2d2, from main; TS only)
Bigger plan (782x690 vs ~470x520), F11 / «На весь экран» full window with the layers drawer (zoom kept), drag-move of buildings with all
their parts (walls, ladders, openings, roof items, route points inside), fence segment / whole ring / end points, ladders (building
constraint), crossings (must stay over the canal), cover; nudge 10 cm / Shift 1 m / Ctrl 10 m; grid snap; undo / redo (arrow runs
coalesced); checks after every move show only problems the move ADDED; dirty flag + confirms; stale derived files listed after accept.
npm test 541 -> 558, typecheck clean. Headless Edge check on L_Gen_Depot_1 (nothing accepted / saved): screenshots in
docs/port/claude/outbox/T022/ on that branch. Review points: the move permission for buildings / bridges is in code (MAP_MOVE), not in
layers.json (Content/Data was off limits); building rotation by gesture not done; props / tactical do not follow a moved building (listed stale).

## T-023 Training Ground props editing (worker/t023-tg-props-edit 77fbb349 + 9939529c, on top of T-022; TS only)
Zoom (wheel at cursor, buttons, %), pan, «Вписать», F11 / Esc via the T-022 helper; zone re-roll by box / polygon / lasso (RegionOverlay):
only new objects inside the zone, outside / locked / pinned / pmc_charges byte-identical, 088 clearance + validator judge the new ones (power
layer skipped: map-wide); drag / nudge a prop with walk-surface snap and auto lock; validation card; undo / redo; unsaved-edit guards on
tab / department / map switch + beforeunload; persistence via the existing «Сохранить». npm test 558 -> 579 (578 pass; combatSim
"10-min run took 1056 ms" wall-clock test failed once under load, 20 / 20 alone), typecheck clean. Headless Edge check on L_Gen_Test_X2:
screenshots docs/port/claude/outbox/T023/; the saved props file restored by git checkout (sha256 matched), current_map.json restored.

## T-021 cold indoors - DONE, merged into this branch (34c832ab)
worker/t021-cold-indoors 86ef2882 (from claude/cold-indoors 01c6cb01 + main). Leafless doorway = open (designer 11-19), test 6/14.
Gate: build OK; ColdShelter 11/11, Environment 114, LevelJson 7, PmcDefence 7, Pmc 70; ColdIndoors / Cold / Environment / Weather / WarmIdle /
PmcRoofDefence / TacticalPmc smokes PASS. Open points for the designer: (1) the committed L_Gen_Depot_1_X6.json (and Depot_1, arena maps) has
cold_rate_mult 0 - the indoor rule shows only where level cold is on; (2) 13 of 70 X6 buildings have no openings in the layout (airtight, no
entrance) - generator doors?; (3) 57 of 70 X6 buildings leak at start; (4) nothing at runtime calls SetOpeningOpen yet (no door / glass actors).
