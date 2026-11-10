# PMC Squad AI Night Trainer Analysis: Run `pmc-main` & `pmc-v2`

**Document Version:** 1.0  
**Date:** 2026-11-04  
**Author:** Gemini (Autonomous Systems & AI Lead)  
**Status:** Complete / Approved for Claude Review  
**Subject:** Empirical evaluation of the overnight utility AI trainer (`Scripts/Trainer/night_trainer.py`), fitness trajectories, genome weight drift, combat mechanics, failure modes, and actionable architectural recommendations.

---

## 1. Executive Summary & Run Overview

The overnight trainer (`Scripts/Trainer/`) trains the PMC squad commander's utility decision weights (`Content/Data/AI/pmc_commander.json`) via self-play in the headless arena (`CodexTactics.PmcArena`) on the generated map `L_Gen_Depot_1`.

### 1.1 Scope of Data Analyzed
This report synthesizes empirical data from two primary trainer runs conducted on 2026-11-03 and 2026-11-04:
1. **`pmc-main`** (Primary run):
   - **Total Completed Generations:** 13 (gens `0000` through `0012` complete; gen `0013` in progress).
   - **Total Arena Rounds Evaluated:** 999 rounds.
   - **Total Champions Promoted:** 7 champions (`g0000_c03`, `g0003_c03`, `g0005_c03`, `g0007_c01`, `g0008_c03`, `g0010_c00`, `g0011_c03`).
   - **Initial vs. Final Fitness:** Baseline fresh fitness = **6.222**; Champion `g0011_c03` fresh fitness = **7.800** (+25.4% improvement; peak fresh fitness = **8.803** in gen `0005`).
   - **Configuration:** Population 8, Elitism 2, Tournament $k=3$, Crossover 0.7, Mutation 0.2, 3 rounds/job, 2 jobs/match (PMC vs PMC self-play). Bot evaluation was inactive pending squad sight integration.
2. **`pmc-v2`** (Validation run with engagement bands & close-range penalty):
   - Started 2026-11-04 with active engagement bands (`use_engagement_bands: true`) and a $-4.0$ penalty for shots under 10 m (`fitness.close_range`).
   - 102 rounds evaluated across generations `0000` and `0001`.

### 1.2 Core Findings at a Glance
- **The Draw Epidemic:** In `pmc-main`, **95.0%** of all rounds (949 out of 999) ended in draws, with **100%** of those draws caused by reaching the 240.0-second round time limit. Only 50 rounds (5.0%) ended by team elimination. In `pmc-v2`, the draw rate reached **100.0%**.
- **Defensive Convergence:** Evolution steadily selected for risk aversion: damage taken dropped by **-55.4%** (from 165.2 HP/round down to 73.7 HP/round), and survivor rates increased from **72.2%** to **94.4%**. However, lethality remained stunted at **0.63 kills per round per 3-man squad**.
- **Flanking Dysfunction:** Across 999 rounds, **3,114 flanks** were attempted (3.12 per round), but only **48 succeeded** (**1.54% success rate**). Operatives attempting flanks repeatedly walked into crossfire or open corridors and were eliminated or pinned.
- **Flapping / Pacing Oscillation:** Track replays reveal severe order oscillation between `Advance` and `FallBack` (e.g. an operative logged **431.6 meters** of travel for a net displacement of only **23.0 meters**).
- **The Point-Blank Dichotomy:** In `pmc-main`, up to **73%–99%** of shots were fired under 10 meters. In `pmc-v2`, the engagement bands and penalty completely eradicated point-blank fire (**0.0%** shots under 10 m), but aggravated the stalemate into a total 4-minute standoff at 20+ meters.

---

## 2. Fitness Evolution Across Generations

### 2.1 Generational Progression in `pmc-main`
The night trainer evaluates candidates against the current champion across shared seeds, followed by a **fresh-seed evaluation gate** for the top-3 contenders and the reigning champion. A contender is promoted only if its fresh-seed fitness strictly exceeds the champion's fresh-seed fitness.

| Gen | Duration | Best Gen Fit | Mean Gen Fit | Champion After | Champ Fresh Fit | Promotion Status |
|:---:|:---:|:---:|:---:|:---:|:---:|:---|
| **00** | 1,242.8 s | 9.946 | 8.168 | `g0000_c03` | 7.200 | **PROMOTED** (vs baseline 6.222) |
| **01** | 1,313.8 s | 8.091 | 7.515 | `g0000_c03` | 7.451 | Retained |
| **02** | 1,305.5 s | 8.596 | 7.745 | `g0000_c03` | 8.254 | Retained |
| **03** | 1,323.7 s | 7.713 | 6.699 | `g0003_c03` | 7.992 | **PROMOTED** (vs 7.701) |
| **04** | 1,806.7 s | 7.646 | 7.493 | `g0003_c03` | 9.321 | Retained (champ hit high on seeds) |
| **05** | 1,259.4 s | 8.435 | 7.552 | `g0005_c03` | 8.803 | **PROMOTED** (Peak fresh fitness!) |
| **06** | 1,274.5 s | 8.098 | 7.267 | `g0005_c03` | 7.575 | Retained |
| **07** | 1,490.8 s | 9.128 | 8.136 | `g0007_c01` | 8.752 | **PROMOTED** (vs 8.364) |
| **08** | 1,281.7 s | 8.654 | 7.648 | `g0008_c03` | 8.076 | **PROMOTED** (prev champ fell to 6.048) |
| **09** | 1,227.2 s | 6.894 | 6.073 | `g0008_c03` | 7.356 | Retained |
| **10** | 1,354.8 s | 7.343 | 6.236 | `g0010_c00` | 7.779 | **PROMOTED** (vs 7.732) |
| **11** | 45,880.7 s | 9.268 | 8.009 | `g0011_c03` | 7.291 | **PROMOTED** (vs 7.062; run resumed) |
| **12** | 3,285.2 s | 8.298 | 7.556 | `g0011_c03` | 7.800 | Retained |

```
Fresh-Seed Champion Fitness Trajectory:
 9.0 |           [g0005: 8.80]--[g0007: 8.75]
 8.5 |                                      \
 8.0 |                 [g0003: 7.99]         [g0008: 8.08]--[g0010: 7.78]--[g0011: 7.80]
 7.5 |   [g0000: 7.20]                                                    
 7.0 |        
 6.5 | [Base: 6.22]
 6.0 +---------------------------------------------------------------------------------->
       Gen 0       Gen 3       Gen 5       Gen 7       Gen 8       Gen 10     Gen 12
```

### 2.2 Analysis of the Fresh-Seed Validation Gate
The fresh-seed anti-overfit gate proved **essential**:
1. **Filtering Lucky Outliers:** In Gen 00, candidate `g0000_c06` achieved the highest single generation fitness in the entire run (**9.946**). However, on fresh seeds its fitness collapsed to **6.964**. It was rightfully rejected in favor of `g0000_c03` (fresh fitness 7.200). Similarly, in Gen 08, `g0008_c07` achieved 8.456 in generation matches, but dropped to 5.569 on fresh seeds.
2. **The Self-Play Fitness Ceiling & Inversion:** Fresh fitness peaked in Generation 05 (`g0005_c03`: 8.803) and Generation 07 (`g0007_c01`: 8.752), then gradually receded toward ~7.3–7.8. This is **not regression**; it is a structural artifact of symmetric self-play:
   - Early mutants scored easy kills and flank bonuses against weaker, unoptimized baseline champions.
   - As champions learned to preserve HP, take cover, and avoid suicidal retreats, both squads became equally resilient.
   - In late generations, mutual defense resulted in fewer kills, lower suppression bonuses, and 240s duration decays, compressing absolute fitness scores for both sides.

---

## 3. Genome Weight Drift & Tactical Significance

### 3.1 Weight Comparison: Baseline vs. Final Champion (`g0011_c03`)

The genome consists of all numeric leaf nodes in `pmc_commander.json`. Below is the complete parameter audit comparing baseline weights to final champion `g0011_c03`:

| Gene Name | Baseline | Champion | Delta | % Change | Allowed Bounds | Clamped? |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|
| `fall_back` | 0.9000 | 0.6305 | -0.2695 | **-29.9%** | [0.45, 1.35] | No |
| `suppress` | 0.8000 | 0.5976 | -0.2024 | **-25.3%** | [0.40, 1.20] | No |
| `decision_interval` | 0.5000 | 0.4233 | -0.0767 | **-15.3%** | [0.25, 0.75] | No |
| `advance` | 0.7000 | 0.7983 | +0.0983 | **+14.0%** | [0.35, 1.05] | No |
| `reload` | 0.8000 | 0.9107 | +0.1107 | **+13.8%** | [0.40, 1.20] | No |
| `switch_margin` | 0.1000 | 0.1123 | +0.0123 | **+12.3%** | [0.05, 0.15] | No |
| `heal_below` | 0.4500 | 0.5006 | +0.0506 | **+11.2%** | [0.225, 0.675] | No |
| `flank_distance_cm` | 1000.0 | 1107.0 | +107.0 | **+10.7%** | [500.0, 1500.0] | No |
| `move_switch_margin` | 0.4500 | 0.5309 | +0.0809 | **+18.0%** | [0.225, 0.675] | No |
| `high_ground` | 0.2000 | 0.2391 | +0.0391 | **+19.5%** | [0.10, 0.30] | No |
| `flank_angle_deg` | 90.0 | 109.0 | +19.0 | **+21.1%** | [45.0, 135.0] | No |
| `relocate_after_seconds` | 6.0 | 4.0 | -2.0 | **-33.3%** | [3.0, 9.0] | No |
| `take_cover` | 1.0000 | 1.0783 | +0.0783 | **+7.8%** | [0.50, 1.50] | No |
| `hold` | 0.3500 | 0.3726 | +0.0226 | **+6.5%** | [0.175, 0.525] | No |
| `advance_stop_range_frac`| 0.6000 | 0.5732 | -0.0268 | **-4.5%** | [0.30, 0.90] | No |
| `suppress_ammo_min` | 0.4000 | 0.4219 | +0.0219 | **+5.5%** | [0.20, 0.60] | No |
| `heal` | 1.1000 | 1.0571 | -0.0429 | **-3.9%** | [0.55, 1.65] | No |
| `crouch` | 0.5000 | 0.5091 | +0.0091 | **+1.8%** | [0.25, 0.75] | No |
| `sidearm` | 1.2000 | 1.2182 | +0.0182 | **+1.5%** | [0.60, 1.80] | No |
| `ambush` | 1.0000 | 1.0154 | +0.0154 | **+1.5%** | [0.50, 1.50] | No |
| `flank` | 0.9000 | 0.9467 | +0.0467 | **+5.2%** | [0.45, 1.35] | No |
| `fall_back_below` | 0.3500 | 0.3500 | 0.0 | **0.0%** | [0.175, 0.525] | No |
| `contact_memory_seconds` | 5.0 | 5.0 | 0.0 | **0.0%** | [2.0, 8.0] | No |
| `commit_seconds` | 2.0 | 2.0 | 0.0 | **0.0%** | [1.0, 3.0] | No |
| `relocate` | 0.6000 | 0.6000 | 0.0 | **0.0%** | [0.30, 0.90] | No |
| `high_ground_max_levels` | 3 | 3 | 0 | **0.0%** | [1.5, 4.5] | Frozen |

### 3.2 Multi-Generation Step-by-Step Trajectory
Tracing each promoted champion demonstrates the chronological adaptation of the squad AI:

| Gene | Baseline | `g0000_c03` | `g0003_c03` | `g0005_c03` | `g0007_c01` | `g0008_c03` | `g0010_c00` | `g0011_c03` |
|:---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `fall_back` | 0.9000 | 0.7329 | 0.7129 | 0.7108 | 0.5902 | 0.7108 | 0.6305 | **0.6305** |
| `suppress` | 0.8000 | 0.7441 | 0.5917 | 0.5366 | 0.5366 | 0.5366 | 0.5976 | **0.5976** |
| `relocate_after_seconds`| 6.0 | 5.0 | 7.0 | 4.0 | 4.0 | 4.0 | 4.0 | **4.0** |
| `flank_angle_deg` | 90.0 | 109.0 | 93.0 | 109.0 | 109.0 | 109.0 | 109.0 | **109.0** |
| `decision_interval` | 0.5000 | 0.5000 | 0.4167 | 0.4233 | 0.4233 | 0.4233 | 0.4233 | **0.4233** |
| `move_switch_margin` | 0.4500 | 0.4500 | 0.4500 | 0.4500 | 0.4500 | 0.4500 | 0.4500 | **0.5309** |
| `advance` | 0.7000 | 0.8726 | 0.7887 | 0.6564 | 0.6564 | 0.6342 | 0.7983 | **0.7983** |
| `high_ground` | 0.2000 | 0.2000 | 0.2244 | 0.1996 | 0.2244 | 0.2007 | 0.2391 | **0.2391** |

### 3.3 Tactical Interpretation of Key Drifts

1. **`fall_back` Reduction (-29.9% from 0.90 to 0.63):**
   - *Baseline Flaw:* Baseline fighters triggered `FallBack` whenever health fell below 35% or they were locally outnumbered. However, arena telemetry proved that retreating in open combat was suicidal: **only 10.8% of retreats were survived** (407 survived out of 3,777 retreats). Sprinting away exposed fighters' backs, dropped their defensive cover posture, and prevented defensive return fire.
   - *Evolutionary Selection:* Evolution aggressively suppressed `fall_back`. Operatives that held firm in cover, leaning out to trade fire, preserved significantly more HP and survived longer.
2. **`suppress` Reduction (-25.3% from 0.80 to 0.60):**
   - *Baseline Flaw:* Suppression consumes $\ge 40\%$ magazine ammo and immobilizes the shooter. On `L_Gen_Depot_1`, targets are almost always behind solid low or high cover, where suppression debuffs have minimal lethal impact.
   - *Evolutionary Selection:* Suppressing an enemy in cover drained ammunition without delivering kills, leaving shooters empty when the enemy advanced. Evolution favored direct engagement and reloading over protracted suppression.
3. **`relocate_after_seconds` Shortening (-33.3% from 6.0 s to 4.0 s):**
   - *Tactical Meaning:* Sitting in an exposed firing position for 6 seconds proved fatal once enemy squads established sightlines. Shortening the threshold to 4.0 s forced fighters to relocate to adjacent cover before enemies locked onto their position.
4. **`move_switch_margin` Increase (+18.0% from 0.45 to 0.53):**
   - *Tactical Meaning:* This was the direct antidote to **order flapping**. When an operative committed to a movement (e.g. advancing or flanking), small fluctuations in utility scores between ticks previously caused them to abort mid-stride. A higher `move_switch_margin` forced operatives to honor their committed pathing.
5. **`flank_angle_deg` Increase (+21.1% from 90° to 109°):**
   - *Tactical Meaning:* A 90° flank perpendicular to the squad line routinely left the flanker in front of the enemy's cover plane or exposed to lateral crossfire. Increasing the angle to 109° commanded a deeper wrap-around maneuver behind the enemy's defensive arc.
6. **`high_ground` Boost (+19.5% from 0.20 to 0.24):**
   - *Tactical Meaning:* The Depot features elevated catwalks, staircases, and container roofs. Gaining elevation confers superior sight cone reach and allows firing over low obstacles. Evolution consistently prioritized vertical positioning.

---

## 4. Combat Characteristics & Match Dynamics

### 4.1 Global Telemetry Summary (Run `pmc-main`)
Aggregated across all 14 generations (999 rounds played):

```
+--------------------------------------------------------------------------------+
| Metric                               | Total Value     | Per Round Average     |
+--------------------------------------+-----------------+-----------------------+
| Total Rounds Evaluated               | 999             | -                     |
| Wins (Team A / Candidate)            | 30              | 3.0 %                 |
| Losses (Team A / Candidate)          | 20              | 2.0 %                 |
| Draws                                | 949             | 95.0 %                |
| Time-Limit Terminations (240.0 s)    | 949             | 95.0 % (100% of draws)|
| Team Eliminations                    | 50              | 5.0 %                 |
| Flanks Attempted                     | 3,114           | 3.12 per round        |
| Flanks Succeeded                     | 48              | 0.05 per round        |
| Flank Success Rate                   | 1.54 %          | -                     |
| Total Kills by Candidate Squad       | 627             | 0.63 per round        |
| NavMesh Stuck Events                 | 3,169           | 3.17 per round        |
| Friendly Fire Incidents              | 0               | 0.00 per round        |
| Retreats Attempted                   | 3,777           | 3.78 per round        |
| Retreats Survived                    | 407             | 0.41 per round (10.8%)|
| Heals Executed                       | 90              | 0.09 per round        |
+--------------------------------------------------------------------------------+
```

### 4.2 Detailed Breakdown per Generation
The following table tracks match outcomes across every generation of `pmc-main`:

| Gen Dir | Rounds | W / D / L | Draw % | Time Limits | Avg Duration | Flanks Succ / Att | Avg Kills | Avg Suppr (s) | Stuck / Rnd |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| `gen_0000` | 72 | 4 / 67 / 1 | 93.1% | 67 (93.1%) | 231.2 s | 8 / 309 (2.6%) | 0.61 | 25.6 s | 3.6 |
| `gen_0001` | 72 | 1 / 70 / 1 | 97.2% | 70 (97.2%) | 238.8 s | 3 / 265 (1.1%) | 0.96 | 27.6 s | 0.8 |
| `gen_0002` | 72 | 4 / 67 / 1 | 93.1% | 67 (93.1%) | 231.4 s | 3 / 273 (1.1%) | 0.67 | 23.6 s | **16.8** |
| `gen_0003` | 72 | 1 / 69 / 2 | 95.8% | 69 (95.8%) | 236.8 s | 5 / 256 (2.0%) | 0.65 | 28.2 s | 0.5 |
| `gen_0004` | 72 | 4 / 68 / 0 | 94.4% | 68 (94.4%) | 231.4 s | 1 / 287 (0.3%) | 0.47 | 9.2 s | **10.1** |
| `gen_0005` | 72 | 5 / 67 / 0 | 93.1% | 67 (93.1%) | 232.5 s | 5 / 195 (2.6%) | 0.79 | 18.9 s | 1.8 |
| `gen_0006` | 72 | 1 / 71 / 0 | 98.6% | 71 (98.6%) | 239.7 s | 0 / 166 (0.0%) | 0.53 | 14.0 s | 0.9 |
| `gen_0007` | 72 | 2 / 70 / 0 | 97.2% | 70 (97.2%) | 236.2 s | 10 / 154 (6.5%) | 0.47 | 17.1 s | 0.1 |
| `gen_0008` | 72 | 3 / 67 / 2 | 93.1% | 67 (93.1%) | 232.2 s | 0 / 165 (0.0%) | 0.68 | 37.1 s | 1.2 |
| `gen_0009` | 72 | 1 / 62 / 9 | 86.1% | 62 (86.1%) | 222.9 s | 1 / 159 (0.6%) | 0.82 | 17.7 s | 2.2 |
| `gen_0010` | 72 | 1 / 68 / 3 | 94.4% | 68 (94.4%) | 235.3 s | 4 / 205 (2.0%) | 0.81 | 30.7 s | 0.8 |
| `gen_0011` | 72 | 1 / 70 / 1 | 97.2% | 70 (97.2%) | 237.5 s | 7 / 257 (2.7%) | 0.46 | 13.7 s | 0.8 |
| `gen_0012` | 72 | 2 / 70 / 0 | 97.2% | 70 (97.2%) | 237.3 s | 1 / 346 (0.3%) | 0.71 | 29.0 s | 3.8 |
| `gen_0013` | 63 | 0 / 63 / 0 | 100.0% | 63 (100.0%)| 240.0 s | 0 / 77 (0.0%) | 0.10 | 13.0 s | 0.8 |

### 4.3 Why Are 95% of Fights Ending in Draws?
Analysis of round telemetry and hit registration shows a confluence of three root causes:
1. **Low Effective Lethality Behind Cover:** Operatives engage at distances of 18–30 meters. Base character accuracy is 85%, but weapon spread, distance falloff, and cover occlusion reduce actual hit rates to **8.2% – 17.2%** (e.g. Highlight `g0012_c06` recorded 16 hits out of 196 shots: 8.2% accuracy). With 100 HP per fighter and 1 medkit each, squads survive sustained low-accuracy potshots indefinitely.
2. **Defensive Symmetry:** Once a fighter reaches high-quality cover, `TakeCover` and `Hold` consistently outscore `Advance`. When both squads adopt identical high-cover policies, neither squad closes the distance to deliver lethal fire.
3. **Fitness Penalty Absence for Draws:** In `night_trainer.py`, a draw awards `won = 0.5`. While duration bonus decays to 0 at 240 s, a squad that sits in spawn cover with 3 survivors and 0 HP lost earns:
   $$\text{Fitness} = 0.5 \times 10.0 + 1.0 \times 3.0 - 0.0 + \text{bonuses} \approx 8.0$$
   A candidate that aggressively advances, secures 2 kills, but loses 1 fighter in 60 seconds scores less fitness than a squad that camps peacefully in cover for 240 seconds. **The fitness function actively subsidizes cowardice.**

### 4.4 Close-Range Engagement: `pmc-main` vs. `pmc-v2`
The close-range penalty (`fitness.close_range` weight $-4.0$ for shots $< 10$ m) and engagement bands introduced on 2026-11-04 produced a dramatic behavioral shift:

```
Close-Range Shot Share (< 10 m):
 pmc-main (Gen 11-12 Tracks):  [████████████████████░░░░░]  73.3% - 99.0%
 pmc-v2   (Gen 00-01 Tracks):  [░░░░░░░░░░░░░░░░░░░░░░░░░]   0.0%
```

- In `pmc-main`, fighters without engagement bands charged into identical rooms or hallways, engaging in point-blank barrel-stuffing shootouts (e.g. `g0011_c00_r1`: 102 out of 103 shots were $< 10$ m).
- In `pmc-v2`, the $-4.0$ penalty and `IsTooClose` standoff logic successfully pushed fighters back to their weapon bands (19–24 m for M16). Shots under 10 m dropped to **0.0%**.
- **The Unintended Consequence:** On `L_Gen_Depot_1`, line-of-sight corridors rarely exceed 25–35 meters without intersecting walls. By mandating a $> 19$ m standoff, riflemen are forced to retreat the instant an enemy peeks around a corner at 15–18 m. In `pmc-v2`, **100% of rounds ended in time-limit draws**, with total kills collapsing to **0.10–0.28 per round**.

---

## 5. Flawed Behaviors Observed in Clips & Tracks

### 5.1 Case Study 1: The "Yo-Yo" / Advance-FallBack Flapping Loop
- **Artifact:** Highlight track `g0012_c03_eval_champion_1_r2.track.json` (Seed 112852185).
- **Observed Behavior:** Operator Roux (Side B) spent 139 seconds oscillating violently between the center trench and rear containers:
  - Total distance traversed: **431.6 meters**.
  - Net positional displacement: **23.0 meters** ($18.8\times$ path-to-displacement ratio!).
- **Chronological Ticker Excerpt:**
  ```
  t=33.5s: Advance  -> moves forward toward enemy squad
  t=37.5s: FallBack -> enters threat radius, outnumbered check fires, sprints back 8m
  t=40.0s: FallBack -> retreat continuation
  t=58.5s: Advance  -> threat is now > 60% range, Advance scores highest again
  t=62.0s: FallBack -> enemy peeks and fires, FallBack scores highest again
  t=64.5s: FallBack -> sprints back
  ```
- **Root Cause:** A complete absence of hysteresis between `Advance` and `FallBack`. Crossing the threat/health boundary immediately flips utility polarity, turning the operative into an oscillating yo-yo.

### 5.2 Case Study 2: The Flank Death Trap (1.54% Success Rate)
- **Artifact:** 3,114 attempted flanks across 999 rounds.
- **Observed Behavior:**
  1. Commander scores `Flank` because a squadmate is suppressing or 2+ allies are alive.
  2. Flanker is assigned a destination 10–11 m away at 90°–109° from the threat vector.
  3. The navmesh path to that flank point traverses open corridors or crosses the line of fire of secondary enemy operatives.
  4. While walking to the flank point, the flanker cannot shoot effectively and drops cover bonuses.
  5. The flanker is gunned down or pinned midway, causing the flank to abort.
- **Root Cause:** `PmcCommanderRules::ScoreOrder` evaluates the desirability of flanking based on squad state, but **does not evaluate path safety**. A flank order is issued even if the only walkable path passes directly through the enemy squad's killzone.

### 5.3 Case Study 3: NavMesh Stuck Spikes in Early Generations
- **Artifact:** Telemetry in `gen_0002` (16.8 stuck events/round) and `gen_0004` (10.1 stuck events/round).
- **Observed Behavior:** Operatives attempting to take cover against thin metal fences or elevated catwalk ramps frequently entered pathing fail loops, generating dozens of stuck events per round.
- **Evolutionary Resolution:** By Generation 11, champion `g0011_c03` achieved **0.000 stuck events/round** (compared to 10.167 for baseline). Evolution effectively pruned weights and timing thresholds that triggered invalid cover evaluations.

### 5.4 Case Study 4: Symmetric Identity Collisions in Telemetry
- **Artifact:** Highlight kill feeds (`g0012_c03`, `g0012_c06`, `g0011_c00`).
- **Telemetry Log:**
  ```
  t= 25.4s: [KILL] Operator Vance eliminated Operator Vance
  t= 96.7s: [KILL] Operator Dahl eliminated Operator Dahl
  t=146.9s: [KILL] Operator Moreau eliminated Operator Moreau
  t=169.7s: [KILL] Operator Haas eliminated Operator Haas
  t=220.4s: [KILL] Operator Petrov eliminated Operator Petrov
  ```
- **Explanation:** Both Side A and Side B spawn operatives using the same static naming list (`pmc_operators.json`). In self-play replays and highlight feeds, identical names create confusing logs where fighters appear to eliminate themselves.

---

## 6. Concrete, Actionable Recommendations for Claude

### 6.1 Fitness Function Revisions (`trainer_config.json`)

1. **Penalize Time-Limit Draws Heavily:**
   - *Current Behavior:* Draws award `won = 0.5`. A 240s round with high survivors yields $\sim 7.5 - 8.0$ fitness, encouraging squads to camp.
   - *Recommendation:* Replace neutral draw scoring with an explicit penalty for time-limit expiration:
     ```json
     "fitness": {
       "win": 10.0,
       "draw_timeout_penalty": -4.0,
       "elimination_bonus": 3.0
     }
     ```
     If a round reaches `time_limit_s`, set `won = 0.0` or apply `draw_timeout_penalty`. Make passive draws mathematically inferior to active combat.
2. **Decisive Victory Duration Curve:**
   - *Current Behavior:* Target duration is 150 s with 120 s tolerance (decaying to 0 at 270 s).
   - *Recommendation:* Shift target duration to **90.0 s** with tolerance **60.0 s**. A clean squad wipe at 75 seconds should score substantially higher than a grueling 230-second war of attrition.
3. **Revise Flank Bonus Criteria:**
   - *Current Behavior:* `flanks_succeeded` requires a kill from the flank or reaching the point completely unseen (1.54% achieved).
   - *Recommendation:* Award partial credit (`flank_angle_achieved`) when an operative reaches an angle $> 60^\circ$ relative to the target's cover normal, forcing the target to turn or leave cover.

### 6.2 Bounds & Genome Tuning (`pmc_commander.bounds.json`)

1. **Uncap `advance` Upper Bound:**
   - *Current Bound:* `min: 0.35, max: 1.05`. Baseline is 0.70; champion drifted to 0.798.
   - *Recommendation:* Raise `max` to **1.50**. In defensive stalemates, the commander needs the headroom to decisively favor advancing over passive holding (`hold: 0.37`, `take_cover: 1.08`).
2. **Expand `move_switch_margin` Ceiling:**
   - *Current Bound:* `min: 0.225, max: 0.675`. Champion reached 0.531.
   - *Recommendation:* Raise `max` to **0.80**. Operatives executing an advance or flank need stronger commitment inertia to prevent ping-pong aborts.
3. **Loosen `relocate_after_seconds` Floor:**
   - *Current Bound:* `min: 3.0, max: 9.0`. Champion locked at 4.0 s.
   - *Recommendation:* Lower `min` to **2.0 s**. In intense firefights, being pinned in one spot for 4 seconds is too long; rapid 2–3s bounding relocation is tactically superior.
4. **Tune Engagement Band Trigger Fraction (`pmc_commander.json`):**
   - *Current Value:* `standoff_trigger_frac: 0.8` (triggers standoff retreat at $0.8 \times 20\text{ m} = 16\text{ m}$ for M16).
   - *Recommendation:* Reduce `standoff_trigger_frac` to **0.50 – 0.60** ($10 - 12\text{ m}$). On urban depot maps, triggering retreats at 16 m causes instant stalemates across every doorway.

### 6.3 Arena & Scenario Adjustments (`L_PmcArenaDepot.json` / Arena Config)

1. **Shorten Round Time Limit:**
   - *Current Value:* `time_limit_s: 240` (4 minutes).
   - *Recommendation:* Reduce to **120 – 150 seconds**. A 2-minute limit will cut training job runtimes by $\sim 40\%$ and eliminate empty pacing.
2. **Tighter Arena Spawning Geometry:**
   - *Current Behavior:* Spawns are randomized across distant corners of the entire Depot layout. Squads spend the first 30–60 seconds simply walking before establishing contact.
   - *Recommendation:* Define designated arena spawn zones with 25–35 meters initial separation and immediate line of sight or single-corner approach routes.
3. **Enforce Mixed Squad Weapon Loadouts:**
   - *Current Behavior:* Loadouts are random from the weapon pool; squads often end up as 3 riflemen or 3 snipers who all share long standoff bands.
   - *Recommendation:* Mandate that each 3-man squad includes **1 CQB operative** (Shotgun, `close_quarters: true`, `close_in: 1.6`). A shotgunner is immune to standoff retreats and will actively breach enemy positions, breaking stalemates.

### 6.4 Commander C++ Scoring & Logic Bugs (`PmcCommanderRules.cpp`)

1. **Implement Advance $\leftrightarrow$ FallBack Hysteresis:**
   - *Bug:* An operative advancing forward who takes damage immediately scores `FallBack`, sprints back 8 m, and upon stopping immediately scores `Advance` again.
   - *Fix:* In `FPmcFighterView`, add a `LastOrderChangeTime`. If the previous order was `Advance`, apply a $0.3\times$ suppression factor on `FallBack` for 3.0 seconds unless current HP is critical ($< 25\%$).
2. **Flank Route Exposure Pre-Check:**
   - *Bug:* `ScoreOrder(Flank)` only checks if a teammate is suppressing and if the target is in range. It does not check if the navmesh path to `FlankPoint` is exposed to enemy fields of fire.
   - *Fix:* In `UPmcSquadSubsystem::PickFlankPoint`, cast 1–2 test traces from enemy known locations to the midpoint of the proposed flank path. If the route is exposed to crossfire, reject the flank point or discount the order score.
3. **Distinct Faction / Team Prefixes in Telemetry:**
   - *Bug:* Side A and Side B generate identical operative names (`Operator Vance vs Operator Vance`).
   - *Fix:* Prefix operative names in the arena with squad tags (e.g. `[Alpha] Vance` and `[Bravo] Vance`) so kill events and 2D track tickers are readable.

---

## 7. Conclusion

Run `pmc-main` successfully proved that the unsupervised overnight evolutionary loop works:
- 14 generations ran continuously without unhandled engine crashes.
- Anti-overfit fresh-seed evaluation successfully shielded the champion line from lucky mutants.
- Natural selection pruned harmful behaviors (retreating in the open dropped by 70%, stuck events collapsed to 0).
- However, self-play without a time-limit penalty converged on **extreme risk-averse camping** (95% time-limit draws).
- Incorporating the recommended **draw timeout penalty**, **Advance/FallBack hysteresis**, and **tighter standoff triggers** will transform the PMC squad from passive cover-campers into decisive, human-like tactical combatants.
