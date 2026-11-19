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

## Baseline (night loop on main 19a90f52, this machine, 30 seeds)
- crossroads-c1: on_pos p50 0, idle_max 19.4 s, crossfire 13.58 deg, gap 2.24 s, spread 4.95 m.
- alley: pending (night loop).

## T-05 phase b
- Diagnosis (seeds 1-2 each, Saved/Logs/SeedBatch/t05-diag-2-*): 0-1 of 3 shooters seated. Causes: (1) candidate starvation - an area
  gather returns 0-3 valid positions from 30-143 scan points; (2) tier 2 (1.8x band) finds FEWER than tier 1 (global stride thins a wider
  ring; slots counted before the ring filter); (3) "forward of the kill zone" (distance to the approach point) drops flank positions;
  (4) phase-b literals in C++ instead of JSON.
- Sonnet subagent fixing (1)-(4) on worker/t05-firing-positions; then the lead runs Crossroads + Alley x30.
