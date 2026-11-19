# Worker night 2026-11-18 -> 19: QD run pmc-v6-qd2 on L_Gen_Depot_X4 (tactical)

## The run

**Run:** pmc-v6-qd2, search mode qd (MAP-Elites), map `L_Gen_Depot_X4`, scenario tactical, parallel 2, on codex-worker.
The config is the committed `trainer_config.json` plus `make_qd_config.py x4`. The clip command includes `nodirector waitcamera`.

**Code:** freeze `80f7d55b`, which contains:
- G008 PMC defence roles;
- G007 ramp fight;
- Antigravity 083 / 083c QD fixes;
- the X4 clip camera `5915bd8a`.

**Time:**

| When | What |
|---|---|
| 17:37 | Started. |
| 17:37-17:51 | Paused: a Codex 047 `build_remote.ps1` replaced the worker tree. A re-freeze followed, so no job ran on foreign code. That incident led to the night guard, `d7f8c3b7`. |
| 08:09 | Stopped at the 08:00 stop-at, after the highlight clips were made. |

**Window and loop:** the window script removed `Saved\Worker\STOP` and restarted the Worker Loop at 08:09; the 08:00 farm
subset is running.

## Result

| | Value |
|---|---|
| Generations | 37 (0-36), ~1550 s each |
| Rounds | 1776; **0 crashes**; no fallback to Depot_1 |
| Archive | **14 / 48 cells** (yesterday's trial: 2 / 48) |
| Champion | `g0036_c04` (promoted 25 times in 37 generations) |
| Best fitness | 21.13 (g0016_c04, archive cell 2,0,0) |
| Best of the last generation | 20.03, mean 13.74 |

**Archive growth:**

| Generation | 0 | 3 | 7 | 9 | 21 | 30 | 35 |
|---|---|---|---|---|---|---|---|
| Cells filled | 3 | 5 | 8 | 9 | 12 | 13 | 14 |

**Best per cell** (axes aggression x mobility x patience; bin edges: aggression 30 / 45 / 60, mobility 16 / 24 / 32,
patience 0.1 / 0.35):

| Cell | Fitness | Candidate | Aggression / mobility / patience |
|---|---|---|---|
| 2,0,0 | 21.13 | g0016_c04 | 48.2 / 6.0 / 0.00 |
| 3,0,0 | 20.59 | g0007_c01 | 60.7 / 7.0 / 0.00 |
| 1,0,0 | 20.03 | g0036_c01 | 41.3 / 9.0 / 0.00 |
| 1,0,1 | 19.35 | g0021_c00 | 37.1 / 7.5 / 0.25 |
| 0,0,0 | 19.34 | g0017_c06 | 25.0 / 12.5 / 0.00 |
| 1,1,0 | 14.79 | g0013_c00 | 33.8 / 17.0 / 0.00 |
| 2,1,0 | 13.85 | g0015_c07 | 47.5 / 19.0 / 0.00 |
| 3,1,0 | 13.08 | g0009_c01 | 66.2 / 16.0 / 0.00 |
| 1,0,2 | 10.88 | g0035_c01 | 41.2 / 10.5 / 0.50 |
| 1,1,1 | 10.25 | g0021_c04 | 32.1 / 16.8 / 0.25 |
| 3,0,1 | 6.92 | g0004_c01 | 61.4 / 15.3 / 0.25 |
| 2,0,1 | 6.75 | g0030_c03 | 49.9 / 4.0 / 0.25 |
| 0,0,1 | 4.85 | g0021_c03 | 24.1 / 7.0 / 0.25 |
| 2,0,2 | 2.31 | g0000_c02 | 49.0 / 7.5 / 0.50 |

**Descriptor histogram per generation** (8 candidates each):

| Axis | What the 37 generations show |
|---|---|
| Aggression | Spreads over all 4 bins. |
| Mobility | Bin 0 (< 16) for 7-8 of 8 every generation. **No candidate ever reached 24+.** |
| Patience | Bin 0 (< 0.1) for 6-8 of 8. Bin 1 is reached by 2-5 candidates in a few generations, bin 2 rarely. |

The full per-generation table (bins, archive, defender metrics, wins) is in `Saved/Reports/worker-2026-11-19/qd3.out` on
the main PC. The source is each generation's `generation.json` and job results on the worker.

**Effective archive space:** mobility bins 2-3 are empty all night, so the reachable space is 4 x 2 x 3 = 24 cells, of which
14 are filled. The mobility axis (edges 16 / 24 / 32) is calibrated above what X4 tactical rounds produce: the night's
range was 4-19.

## Defender metrics (G008)

Round-level `defence` block, per generation:

| Metric | Value |
|---|---|
| `spread_m` | Mean 19-50 m (median 12-46 m), varying by generation with no trend. |
| `shared_cover_pairs` | Mean 0.00-0.29. |
| `objective_share` | 0.02-0.11. |
| `guard_share` | **None in all 1776 rounds.** |
| Defender `overwatch_kills` | **0** in all rounds (as before G008b). |
| Defender `ambush_success` | 0-8 per generation. |

**Finding for the lead / G008:**
- `guard_share` = None means the access-guard role never resolved an access point on X4 in the tactical scenario. G008 reports
  n/a when no access is found. Its smoke `PmcRoofDefenceSmoke` passes on its own map; the tactical scenario's posts on X4 never
  produce a guard.
- The spread is much wider than the smoke's 8-12 m, because the tactical posts lie along the 160 m route.

## Clips (new camera)

| Clip | Length | Round |
|---|---|---|
| `g0036_c04_fresh_archive_0_r2` | 74 s | PMC won 3-0, 71 s round: suppress + ambush + retreat |
| `g0022_c01_eval_archive_0_r0` | 102 s | 2-0, 137 s round: suppress + ambush + retreat |
| `g0002_c06_eval_archive_0_r2` | 56 s | 2-0, 189 s round: suppress + ambush + retreat + heal |
| `g0025_c01_eval_archive_0_r1` | 46 s | champion, 1-2, 240 s round |

**Camera check:**
- **No debug HUD on frame 0** in any clip (`waitcamera` works).
- **In contact the frame is tight and readable.** The g0036 mid-frame shows 6 fighters at ~20 m camera distance with team
  colours clear. The g0022 mid-frame frames the fight at the goal ring.
- **Residuals:**
  1. Frame 0 to ~1 s shows grey, still-streaming snow textures, and the attackers enter the frame by ~1 s.
  2. World-space debug labels ("GOAL 1", route labels, mirrored) are visible in the clips. They are level-gen debug text, not
     the HUD.

## For the lead

1. **G008 guard role never fires on X4 tactical** (`guard_share` None in 1776 / 1776 rounds). Either the tactical scenario
   does not assign the guard, or the access lookup finds nothing on X4.
2. **QD axes:** mobility edges 16 / 24 / 32 are unreachable on X4 (max 19). Patience is mostly 0. Recalibrating the edges
   would roughly double the usable archive. This is Antigravity 083 territory.
3. **Overwatch kills are still 0;** G008b is queued.
4. **Clip polish (small):**
   - hide the world-space debug labels when Record=1;
   - start the capture ~1 s later, or wait for texture streaming.
