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

## T-021 cold indoors - DONE, merged into this branch (34c832ab)
worker/t021-cold-indoors 86ef2882 (from claude/cold-indoors 01c6cb01 + main). Leafless doorway = open (designer 11-19), test 6/14.
Gate: build OK; ColdShelter 11/11, Environment 114, LevelJson 7, PmcDefence 7, Pmc 70; ColdIndoors / Cold / Environment / Weather / WarmIdle /
PmcRoofDefence / TacticalPmc smokes PASS. Open points for the designer: (1) the committed L_Gen_Depot_1_X6.json (and Depot_1, arena maps) has
cold_rate_mult 0 - the indoor rule shows only where level cold is on; (2) 13 of 70 X6 buildings have no openings in the layout (airtight, no
entrance) - generator doors?; (3) 57 of 70 X6 buildings leak at start; (4) nothing at runtime calls SetOpeningOpen yet (no door / glass actors).
