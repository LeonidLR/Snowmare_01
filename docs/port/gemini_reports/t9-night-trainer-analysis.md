# Task Report: T9 — PMC Squad AI Night Trainer Analysis

**Task:** T9 (any time, READ-ONLY). Analyse the night trainer's results  
**Branch:** `gemini/t9-night-trainer-analysis`  
**Date:** 2026-11-04  
**Author:** Gemini (Autonomous Systems & AI Lead)  
**Status:** ✅ Completed

---

## 1. Overview & Deliverables
The night trainer (`Scripts/Trainer/night_trainer.py`) automatically trains the PMC squad commander's utility weights in `Content/Data/AI/pmc_commander.json` via headless self-play on `L_Gen_Depot_1`. Per the weekend task specification:
- Strictly read-only analysis of the night trainer's results for runs `pmc-main` and `pmc-v2`.
- Zero edits to `Scripts/Trainer/`, `Saved/Trainer/`, `Content/Data/AI/`, or any `.umap` files.
- Deliverable document authored: [`docs/design/PMC_TRAINING_ANALYSIS.md`](file:///C:/Users/Zephyrus15Duo/Documents/Unreal%20Projects/CodexTactics-gemini/docs/design/PMC_TRAINING_ANALYSIS.md).

---

## 2. Key Findings & Data Summary

### 2.1 Run `pmc-main` Telemetry (999 Rounds Across 14 Generations)
- **Fitness Evolution:**
  - Baseline fresh fitness: **6.222**
  - Final Champion (`g0011_c03`) fresh fitness: **7.800** (+25.4% improvement)
  - Peak champion fresh fitness: **8.803** (`g0005_c03`) and **8.752** (`g0007_c01`)
  - Total promotions: 7 champions promoted across 14 generations.
  - The fresh-seed anti-overfit gate successfully filtered out overfit candidates (e.g. `g0000_c06` had generation fitness 9.946 but fresh fitness 6.964; `g0008_c07` dropped from 8.456 to 5.569).
- **Match Outcomes & The Draw Epidemic:**
  - **Wins:** 30 (3.0%)
  - **Losses:** 20 (2.0%)
  - **Draws:** 949 (**95.0%**)
  - **Time-Limit Expirations (240.0 s):** 949 (**100.0% of all draws**)
  - **Team Eliminations:** 50 (5.0%)
- **Combat Mechanics:**
  - **Kills:** 627 total kills by candidate squads (average **0.63 kills per round per 3-man squad**).
  - **Flanking Dysfunction:** 3,114 flanks attempted, only **48 succeeded** (**1.54% success rate**). Flankers routinely walked across open corridors or crossfire and were pinned/killed.
  - **Retreat Mortality:** 3,777 retreats attempted, only **407 survived** (**10.8% survival rate**). Falling back in the open was suicidal.
  - **Stuck Events:** Spiked to 16.8/round in Gen 2 and 10.1 in Gen 4, but dropped to **0.000** in final champion `g0011_c03`.

### 2.2 Genome Weight Drift
- `fall_back`: **-29.9%** (0.90 $\to$ 0.63). Natural selection severely penalized retreating because retreating operatives had a 10.8% survival rate.
- `suppress`: **-25.3%** (0.80 $\to$ 0.60). Suppression wasted ammo and locked the operative in place without delivering lethal damage.
- `relocate_after_seconds`: **-33.3%** (6.0 s $\to$ 4.0 s). Sitting still while exposed was dangerous; repositioning sooner reduced damage taken.
- `move_switch_margin`: **+18.0%** (0.45 $\to$ 0.53). Direct evolutionary response to flapping, forcing operatives to commit to movement.
- `flank_angle_deg`: **+21.1%** (90° $\to$ 109°). Flanking perpendicular (90°) crossed front sightlines; 109° commanded deeper envelopment.
- `decision_interval`: **-15.3%** (0.50 s $\to$ 0.42 s). Quicker reaction cycles.
- `high_ground`: **+19.5%** (0.20 $\to$ 0.24). Rewarded elevation on Depot catwalks and roofs.
- `advance`: **+14.0%** (0.70 $\to$ 0.80). Sustained contact pressure.

### 2.3 Close-Range Engagement: `pmc-main` vs. `pmc-v2`
- `pmc-main` (no engagement bands, no close-range penalty): **73.3% – 99.0%** of shots were fired under 10 meters (point-blank shootouts).
- `pmc-v2` (engagement bands + $-4.0$ penalty for $< 10$ m): **0.0%** of shots were under 10 meters!
- *Side effect:* Because riflemen back off whenever a threat is $< 19$ m on narrow depot streets, `pmc-v2` produced **100% time-limit draws** with only 0.10–0.28 kills per round.

---

## 3. Flawed Behaviors Observed
1. **Yo-Yo Flapping Loop:** `Advance` $\leftrightarrow$ `FallBack` oscillation without hysteresis (Operator Roux logged 431 m of travel for 23 m net displacement).
2. **Flank Death Traps:** Flanking does not check route exposure, sending operatives on 11m walks through enemy fields of fire (98.5% failure rate).
3. **Mutual Standoff Camping:** Both squads entrench in high cover at 20–25m and wait out the 240s timer.
4. **Duplicate Fighter Names:** Side A and Side B share identical names ("Operator Vance eliminated Operator Vance").

---

## 4. Key Recommendations for Claude
1. **Fitness Function:** Apply a heavy penalty for time-limit expiration (`draw_timeout_penalty: -4.0` or `won: 0.0` on timeout) and shift target duration from 150s to 90s.
2. **Bounds Tuning:** Uncap `advance` max to 1.50, increase `move_switch_margin` max to 0.80, and reduce `standoff_trigger_frac` from 0.8 to 0.5–0.6 ($10 - 12$ m).
3. **Scoring Logic:** Implement an `Advance` $\leftrightarrow$ `FallBack` hysteresis timer (suppress `FallBack` for 3s after `Advance`), and check line-of-sight exposure on proposed flank routes.
4. **Arena Setup:** Reduce round time limit from 240s to 120–150s, use closer spawns (25–35m), and guarantee at least 1 CQB/shotgunner per squad to force engagements.

---

## 5. Verification
- Checked directory `Saved/Trainer/pmc-main/` and `Saved/Trainer/pmc-v2/` read-only.
- Extracted and verified metrics using Python scripts (`leaderboard.json`, `generation.json`, round files, track files).
- Zero modifications made to engine, code, trainer, or data files.
