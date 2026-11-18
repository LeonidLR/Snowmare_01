# codex-worker night report — 2026-11-17 19:00 → 2026-11-18 08:20

The night used a one-off schedule set by the designer:

| Time | Work on codex-worker |
|---|---|
| 19:00-24:00 | Build-farm trial: remote compiles and farm smokes |
| 23:45 | Freeze on `d5c4ae34`, built on the worker |
| 00:00 | Full smoke sweep |
| 06:00-08:00 | Quality-diversity (QD) trainer live trial |
| 08:00 | Test farm and the daily smoke subset |

The ga trainer `pmc-v5` was stopped at 18:30 for the build baseline, so it did not run overnight; see section 3.

Raw data is on the main PC in `Saved/Reports/worker-2026-11-18/`:
- `pmc-v5/` and `pmc-v6-qd/`: the run folders;
- `clips/`: the chosen clips;
- `sweep-result.json`, `qd_window.log`, `qd_metrics.txt`, `*-stalemate.txt`.

## 1. Smoke sweep 00:00-01:58 (all registry smokes, `d5c4ae34`, built on the worker)

**296 run: 285 PASS, 2 PASS only on the re-run, 9 FAIL** (each FAIL was re-run once alone and failed again). It took 1 h 58 min wall time (7,100 s), 2 engines at a time, with `-Multiprocess` and a fixed time step. After it the worker sat idle from 02:00 to 06:00.

| Smoke | Result | First FAIL line (`-alone` log) |
|---|---|---|
| GeneratedPropsX6Smoke | FAIL | longest spawn frame 68.1 ms <= 60 ms budget |
| CoverCrouchSmoke | FAIL | crouched corner burst: 0 shots, crch fire_L / crouch-shimmy to his left, threat on his right: backwards |
| PauseBarrelOrderSmoke | FAIL | real time: "Ignite" ran, barrel 4 burns, one match spent / the Ctrl + click shot ignited barrel 5 |
| TutorialSmoke | FAIL | medkit lesson: H used his medkit (healed, one medkit fewer) / the POSTURE card is up (queued) |
| PeekMinesSmoke | FAIL | rev 5j: each role in cover at his own ring - the card done (20.0 s) / the rings are gone |
| SuppressionSmoke | FAIL | F: the pack splits round it (6 left, 0 right) |
| LevelGenRampSmoke | FAIL | B_warehouse_stair_0 group (all three box-selected, sprint): STUCK after 16 s (top level z 900), Commander at X=4855 Y=1166, speed 0, path complete, ends 0 cm from the goal |
| CorpseCostSmoke | FAIL | 100 more frozen bodies (50 -> 150) add 1.21 ms of game-thread time (budget 0.5 ms) — the known G004 finding |
| FormationMantleSmoke | FAIL | c: formation, double click on a 135 cm fence's face: Engineer waited his turn ... 29 bad / e: ground click beyond the fence: Medic-Sapper ... 5 bad |
| RainSmoke | PASS on re-run | new flaky (not in `knownFlaky`) |
| SightConditionsSmoke | PASS on re-run | new flaky (not in `knownFlaky`) |

Comparison with the Codex 041 day sweep (17.11, base `50e8e4a9`, 6 failing twice):
- Still failing: PauseBarrelOrder, Tutorial, PeekMines, LevelGenRamp.
- Passed tonight: FormationJitter, StealthMode.
- New failures tonight: GeneratedPropsX6, CoverCrouch, Suppression, CorpseCost (expected, G004), FormationMantle.

## 2. Build farm 19:00-24:00 (remote compile, single slot)

- **26 builds ran on the worker**, for 6 agent checkouts: claude, gemcli, gemday, grok, grok2, tactical.
  - 22 succeeded.
  - 1 failed to compile: gemcli at 19:23, a real compile error after UBT 352 s.
  - 3 failed within 20 s, before UBT started: gemcli at 23:24 and 23:26, gemday `43af4043` at 23:25.
- **UBT time of the successful builds: median 314 s** (min 31, max 387).
  - Only 2 builds were genuine incrementals (31 s and 32 s), and 1 was partial (159 s).
  - Every other build was close to a full rebuild. The reason: each caller ships a different HEAD into the one shared worker tree, so a build undoes the tree state the previous caller left.
- **Door to door, as the caller saw it (27 records): median 7.7 min** (min 1.1, max 27.0).
  - 10 of 22 successful builds took over 10 min, mostly waiting for the single slot.
  - 1 worker error (gemday at 20:07, 30 min): it waited on a stale lock left by a killed caller (PID 37944).
- **Stale locks.** Fixed in `bce1dc66`: `worker_sync` now reclaims dead syncs and logs each one. 11 reclaims were logged during the night (`Saved/Worker/stale_locks.log`): 7 at 20:57, 1 at 21:34, 3 at 23:13-23:24. They were all callers killed mid-run, typically by an agent's 2-min tool timeout.
- **Reference numbers** (idle worker, 17.11 evening):

| Build | UBT | Door to door | Sent back |
|---|---|---|---|
| Clean | 432.5 s | 8.1 min | 214 MB |
| Incremental, one .cpp | 35.2 s | 1.2 min | 15 MB |
| Same-build smoke (no sync) | — | PASS in 42 s | — |

- **Farm smoke batches** (agents' `smoke_parallel -Remote auto`): 6 batches, 14 smokes, 4 not PASS (gemday 3 of 9, grok2 1 of 5).
- **Follow-up:** two build slots with tree affinity. The designer approved it via the lead; it will be measured after this report. The single shared tree is the bigger cost: per-slot tree reuse should turn most of the ~5 min rebuilds into ~1 min incrementals.

## 3. pmc-v5 (ga) — 15:49-18:31 on 17.11, then stopped

The trainer ran only before the 18:30 stop for the build baseline. The night plan kept 00-06 for the sweep and 06-08 for QD, so pmc-v5 did not run overnight.

- **8 generations (0-7)** in 2 h 40 min, 15-20 min per generation, 204 champion-vs-champion jobs, 612 rounds, no crashes.
- **Champion line:** g0000_c07 → g0001_c02 → g0003_c01 → g0004_c03 → g0005_c03 → g0006_c04 → **g0007_c07**.
  - The champion was replaced in every generation from 3 on.
  - The fresh fitness of the new champions fell from 18.5 (g0004_c03) to 9.1 (g0007_c07). That points to an unstable self-play target, not steady progress.
- **Stalemate:**
  - 26 of 612 rounds (4.2 %) had no contact at all;
  - 51 rounds (8.3 %) ended on the time limit, 30 of them as draws.
- **Quiet spells** (the longest gap between shots in each round): median 14.3 s, 90th percentile 53.6 s. 83 rounds had a gap over 45 s and 22 rounds a gap over 90 s.
- **Fight tempo:** first contact at 14 s on average, 115 shots per minute, the attackers' closest approach 1 m.
- **Orders used** (45,910 commander decisions): FallBack 17 %, Suppress 17 %, Objective 17 %, TakeCover 14 %, Hold 8 %, Flank 6 %, Heal 5 %, Crouch 4 %, Ambush 4 %, Reload 4 %, Advance 4 %, RpgStrike 1 %, Relocate and Sidearm ~0 %.
  - FallBack is as frequent as Objective. Relocate and Sidearm are almost never chosen.

### Clips (judged by watching contact sheets, not by fitness)

**Best 3:**
1. `best1-v5-g0007_c07-wipe-40s.mp4`: a 3-0 wipe at a wall corner in 40 s, 282 shots per minute, no quiet spell. Readable: both sides are on screen.
2. `best2-v5-g0007_c05-suppress-heal-48s.mp4`: suppress, retreat and heal, won 1-0 in 48 s, continuous fire.
3. `best3-v5-g0004_c05-flank-blizzard-84s.mp4`: the only clip with a flank (flank, suppress, retreat, heal) in 84 s. It is a blizzard round, so the fog makes it hard to read, and the first frame shows the debug HUD.

**Worst 3:**
1. `worst1-v5-g0000_c00-far-camera.mp4`: generation 0. The camera is so high the fighters are a few pixels. Unreadable.
2. `worst2-v5-g0000_c01-far-camera.mp4`: the same problem.
3. `worst3-v5-g0001_c02-46s-quiet-137s.mp4`: a 46.5 s quiet spell in a 137 s round, and the candidate loses to the champion.

## 4. QD live trial 06:00-08:09 — `pmc-v6-qd` on L_Gen_Depot_X4, Scenario=tactical

**Setup** (`Saved/Worker/qd_window.log`):
- The sweep had ended (01:58).
- Preflight: PmcArenaTacticalSmoke from the sweep was **PASS**, so the trainer took **variant x4**: run `pmc-v6-qd`, `search.mode qd`, map L_Gen_Depot_X4, `Scenario=tactical`, `-LevelJson=L_Gen_Depot_X4.json`. This used a copied config; the committed default (ga) is unchanged.
- No fallback was needed. The trainer exited 0 after 129 min (stop-at plus highlight clips).

**Generations:**
- 5 generations finished (0-4), about 20.7 min each, plus a partial generation 5.
- 138 arena jobs, 276 rounds, every one a tactical round with the tactical-map flag set.
- League: 57 archive jobs, 69 bot jobs, 12 champion jobs.
- Champion line: g0000_c07 → g0001_c03 → **g0002_c05**, unchanged in generations 3-4. Best candidate fitness: 10.14 (g0004_c06).

### 4.1 Did all 11 behaviour metrics arrive?

**Yes.** All 11 metrics are present and finite in all 828 behaviour blocks: `rounds[].behaviour` and `sides.A.behaviour` / `sides.B.behaviour` in each of 276 rounds, schema `codex.pmc_arena_behaviour/1`. There were 0 missing values, 0 non-finite values and 0 all-zero blocks.

| Metric | Non-zero blocks | Mean | Max |
|---|---|---|---|
| time_in_contact_s | 792 / 828 | 41.6 | 129.8 |
| flanks_completed | 92 / 828 | 0.13 | 3 |
| moves | 759 / 828 | 19.1 | 150 |
| suppress_then_move | 459 / 828 | 1.38 | 10 |
| ambush_success | 96 / 828 | 0.14 | 3 |
| **overwatch_kills** | **0 / 828** | 0 | 0 |
| time_in_chokepoint_s | 793 / 828 | 9.2 | 137 |
| window_positions_used | 452 / 828 | 0.89 | 5 |
| quiet_spells | 483 / 828 | 0.95 | 5 |
| order_churn | 751 / 828 | 33.6 | 204 |
| stalemate_share | 72 / 828 | 0.09 | 1.0 |

**overwatch_kills is always 0.** The value never moves on X4: either no fighter ever fires from an overwatch roof spot, or the spot test (`IsAtOverwatchSpot`, floor tolerance) never matches. This is worth a look.

### 4.2 How many archive cells are filled?

**2 of 48.** The archive did not grow after generation 0, except that a better occupant replaced one cell in generation 4.

**Cause: the mobility axis saturates.**
- Its descriptor is `flanks_completed + moves` with range [0, 12] in 4 bins, but `moves` averages 19 per round.
- So every candidate falls in mobility bin 3.
- Aggression (`time_in_contact_s`, range [0, 180]) puts almost everyone in bins 0-1.
- Patience (`ambush_success`, range [0, 3]) averages 0.14, so everyone is in bin 0.

**Suggestion for the lead (trainer_config, not changed here):**
- mobility: range about [0, 60];
- aggression: range about [0, 120];
- patience: per-round average × 4, or `suppress_then_move`.

The archive cannot diversify until the axes are rescaled.

### 4.3 Best result per cell

| Cell (aggression, mobility, patience) | Occupant | Generation | Fitness | Descriptor |
|---|---|---|---|---|
| 0,3,0 | g0000_c01 | 0 | 9.93 | contact 28.3 s, mobility 10.0, ambush 0.25 |
| 1,3,0 | g0004_c06 | 4 | 10.14 | contact 55.9 s, mobility 21.75, ambush 0.0 |

### 4.4 Crashes, fallbacks, zero metrics

- No engine crashes, no fallback to zero metrics, and no fallback to the Depot_1 variant.
- **Artificial "crashes" in every bot job.**
  - All 69 bot jobs returned **1 round** (the arena plays a single round against the bot). The trainer asks for 3 and pads the 2 missing rounds as crashes (`pad_missing_rounds_as_crash`, `crash_penalty -5`).
  - Result: 138 padded "crashed" rounds, and every candidate's bot leg loses about 10 fitness points.
  - The penalty is the same for everyone, so the ranking survives, but crash counts and fitness are distorted.
  - Fix in the league config: `rounds_per_job 1` for bot jobs, or let the arena play N rounds against the bot.
- **The bot does not engage on X4.** In the bot rounds the attackers' closest approach is 38-68 m, and 2 of the 6 bot rounds in the last generation were 240 s time-limit draws with 195-209 s of silence. This matches the known note that the bot does not see hostile operatives. The bot leg tells us little until that is fixed.

**Stalemate (QD, all 276 rounds):**
- 10 rounds (3.6 %) had no contact.
- 39 rounds (14.1 %) ended on the time limit.
- Longest quiet spell: median 19.5 s, 90th percentile 82.3 s.
- The attackers' mean distance to the objective is 68 m on X4, against 23 m on Depot_1: a much larger map.

**Orders used (QD):** Objective 23 %, TakeCover 19 %, FallBack 16 %, Suppress 11 %, Hold 7 %, Advance 6 %, Flank 5 %, Heal 4 %, Crouch 4 %, Ambush 2 %, Reload 2 %, RpgStrike 1 %.

### 4.5 QD clips

| Clip | What happens | Readability |
|---|---|---|
| `qd-x4-champion-ambush-wipe-36s.mp4` | Ambush and retreat, a 3-0 wipe in 36 s | — |
| `qd-x4-g0003_c02-flank-ambush-168s.mp4` | Flank, suppress, ambush, retreat, heal; the richest round | — |
| `qd-x4-g0003_c01-201s-quiet.mp4` | 201 s round with a 46 s quiet spell; the champion wins | Worst |

**Readability problem on X4:** the clip camera sits far too high for the larger map, so the fighters are barely visible. The first frame shows the debug HUD, and one clip opens with heavy motion blur. The clip camera needs a map-scaled zoom before QD clips are useful to the designer.

## 5. After 08:00

- **Worker loop restarted at 08:09.** The QD window removed STOP and started the loop task.
- **Daily subset** (`worker_daily_smokes.json`, 18 smokes, 08:09-08:17): **17 PASS, 1 FAIL — LevelGenRampSmoke**, the same stuck point at the top of `B_warehouse_stair_0` as in the sweep.
- **Test farm:** serving remote batches until 16:30.

## 6. Tasks for the lead

1. **QD axes:** rescale mobility and aggression (patience too) in `trainer_config` before the next QD run. With the current ranges the archive stays at 2 of 48 cells.
2. **League bot leg:** use `rounds_per_job 1` for bot jobs; this ends the padded "crashes". Then fix the bot's engagement on X4.
3. **overwatch_kills is always 0 on X4:** check the overwatch-spot detection.
4. **Clip camera on X4:** use a zoom scaled to the map, and hide the debug HUD in the first frame.
5. **Smokes:**
   - New failures: GeneratedPropsX6 (68 ms spawn frame), CoverCrouch, Suppression, FormationMantle.
   - Still failing: PauseBarrelOrder, Tutorial, PeekMines, LevelGenRamp.
   - New flaky: RainSmoke and SightConditionsSmoke.
6. **pmc-v5:** the champion line is unstable (fresh fitness fell from 18.5 to 9.1). Relocate and Sidearm are never chosen.
