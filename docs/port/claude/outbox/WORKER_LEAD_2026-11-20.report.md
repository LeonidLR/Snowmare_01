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
Folders: C:\Work\CodexTactics\Saved\Worker
ightloop6-11-19\digest.log; slot1 Saved\Logs\SeedBatch\dayloop-c4f8f561-{cross,alley}.
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
| crossroads T-05 kill zone 0.25 | 4588fbbc | 0.33 | 3.1 | 22.8 | 1.85 |
| alley T-05 kill zone 0.25 | 4588fbbc | 0 | 8.6 | 18.9 | 2.33 |

## T-05 state (worker/t05-firing-positions 4588fbbc, gated, NOT accepted)
Iteration 2: ground kill zone on the attackers' nav path (kill_zone_reach_factor; 10-seed sweep 0.6 / 0.4 / 0.25 -> 0.25). Gate: build OK,
FiringPosition 11/11, Pmc 70/70, 7 smokes PASS. Findings + next step in the T-05 report handover: seated pairs reach 47-72 deg, but the
defenders spawn ~8 m from the objective and are caught walking; next = seat only positions reachable before the attackers' expected arrival.
Alley: some approach points have no ground cover that sees them (height / mobile anvil = T-08b in the roadmap) - acceptance fork for the main lead.

## T-021 cold indoors - DONE, merged into this branch (34c832ab)
worker/t021-cold-indoors 86ef2882 (from claude/cold-indoors 01c6cb01 + main). Leafless doorway = open (designer 11-19), test 6/14.
Gate: build OK; ColdShelter 11/11, Environment 114, LevelJson 7, PmcDefence 7, Pmc 70; ColdIndoors / Cold / Environment / Weather / WarmIdle /
PmcRoofDefence / TacticalPmc smokes PASS. Open points for the designer: (1) the committed L_Gen_Depot_1_X6.json (and Depot_1, arena maps) has
cold_rate_mult 0 - the indoor rule shows only where level cold is on; (2) 13 of 70 X6 buildings have no openings in the layout (airtight, no
entrance) - generator doors?; (3) 57 of 70 X6 buildings leak at start; (4) nothing at runtime calls SetOpeningOpen yet (no door / glass actors).
