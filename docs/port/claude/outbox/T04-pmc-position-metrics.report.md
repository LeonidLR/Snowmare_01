# T-04 - position metrics of the arena + the Crossroads / Alley smokes (smart-enemies)

Branch `claude/t04-pmc-position-metrics`, built on Codex's unfinished WIP (`codex/049-pmc-position-metrics`, commit `a20fa242`, kept untouched as the base).

## Handover
Done and verified locally: part a (five metrics) and part b (two red smokes, stand-in layouts). Open: the smokes should move to the T-02 lab maps `L_Lab_Crossroads` / `L_Lab_Alley` and a scripted opponent (T-03) when those merge (only `map` / `extra` in `Scripts/test_map.json` + an `Opponent=Script` argument change); `PmcBridgeSmoke` (A-00C) is not written (the brief named the two clips). Not measured, listed below.

## What Codex had vs what I changed
Codex's WIP (sound, kept): pure rules `FPmcArenaPositionTime::AddInterval` (open idle, exposed run), `FPmcArenaOpeningShots` (first-shot gap), `AttackersInRange`, `OnPositionShare`, `CrossfireDeg`; the arena sampling in `PmcArenaCommand.cpp` (previous sample holds until the next, 0.2 s track step, 16-direction cover-slot probe within 2 m, cover / post occupancy, physical movement) and 3 unit tests. It built and its 3 tests passed as left.

My changes:
1. **Contact definition (a real defect).** Codex measured "the first attacker within weapon range" with the weapon's MAX reach. Run 1 of the new smokes: in range at 0.0 s (spawns 78-88 m apart), so `open_idle_s.before_contact_s` was always 0 and `on_position_before_contact` measured the spawn. Using the EFFECTIVE reach was still wrong (a marksman carries 90 m: in reach at 0.0 s, closest pair 77.6 m, run 2). Now: `SquadReachCm` = the MEDIAN of the living armed defenders' effective reach (28 m here), `AttackersInRange` uses it. Unit tests extended (median, marksman outlier, two guns, no armed man).
2. New field `rounds[].defence.range_contact_s` (time of that first in-range sample, null without) so a smoke can see that "before contact" is not empty - the smallest record, as the stop rule asks.
3. `CodexTactics.PmcCrossroadsSmoke` (A-00A, scenario street) and `CodexTactics.PmcAlleySmoke` (A-00B, scenario chokepoint) in `PmcArenaCommand.cpp` (`JudgeAmbushPositions`), registered in `Scripts/test_map.json` (registry, `smokeOptions` with `knownFail: true` + `_why`, the tactical-pmc rule's smokes). Preconditions are logged as "precondition", criteria as "A-00A.n" / "A-00B.n", so a red run says which it is.
4. G002 note updated (the contact definition, `range_contact_s`).
Codex's report (Russian, `docs/port/codex/outbox/049-...report.md`) is left as it was.

## Smoke design and the honest limit
T-02 (lab maps) and T-03 (scripted opponent) are not on main, so the smokes run a 3v3 objective round on the Depot (`L_Gen_Depot_1`, `L_PmcArenaDepot.json`): an open street site (7 of 8 chest rays free over 10 m) = Crossroads stand-in, a 1-3 m passage between 3 m walls = Alley stand-in; attackers are self-play PMC, not "running straight". Thresholds are the GDD's (open idle <= 2 s, crossfire >= 45 deg, first shots <= 1.5 s apart: named constexpr with the GDD id, since `squad_plays.json` does not exist before T-08); the alley spacing is `min_spacing_m` from `pmc_commander.json`. No threshold was tuned to the current code.

Checked: A-00A.1 (idle, on position), .2 (crossfire), .4 (first-shot gap); A-00B.1 (idle, on position), .2 (spacing; crossfire).
NOT measured (needs data the arena does not record): anvil inside the enclosure / hidden from the kill zone, roof role and ladder use, the lane covered by three fields of fire, "attackers cannot leave", the 20-seed rates, the F4 gate, the PL-01 attackers (A-00A.3, .5, .6; A-00B.1 roles, .3, .5). G008's sector posts are not in the public snapshot; only `GetFighterPost` posts count as "on position".

## Metrics from real runs (fixed-step smoke runs, 3v3, seed 1; the last pair of runs)
| metric | Crossroads (street) | Alley (chokepoint) |
|---|---|---|
| first attacker in squad reach (`range_contact_s`) | 7.3 s (closest pair 27.9 m, median reach 28.0 m) | 7.9 s (27.2 m) |
| worst `open_idle_s.before_contact_s` | 5.50 s | 1.90 s |
| `open_idle_s.total_s` (worst man) | 26.4 s | 18.4 s |
| `on_position_before_contact` | 0.00 | 0.33 |
| `crossfire_deg` | 7.2 | 11.6 |
| `first_shot_gap_s` | 3.75 | 0.28 |
| `max_exposed_run_m` (attackers) | 23.2 | 27.4 |
| mean spread (G008) | 5.7 m | 4.1 m |
Every metric is non-zero in at least one real run. Earlier runs (before the contact fix) gave spread 4.3-20.8 m, so single numbers vary between runs; every run was red on at least `on_position`, `crossfire` or the gap.

Red result (both KNOWN-FAIL in smoke_parallel, preconditions green): Crossroads fails A-00A.1 idle (5.50 > 2.0), A-00A.1 on position (0.00), A-00A.2 crossfire (7.2 < 45), A-00A.4 gap (3.75 > 1.5). Alley fails A-00B.1 on position (0.33), A-00B.2 crossfire (11.6 < 45), A-00B.2 spread (4.1 < 5.0); its idle check passes at 1.90 s (borderline; it is the one check that can flip between runs, and no threshold was moved for it). That matches the audit: nobody takes cover before contact, no crossing positions, the squad stands bunched.

## Gate
- `Scripts/build.ps1` local: PASS (last run 2026-11-19 ~12:30; the lead asked for the worker build: it built on the worker in 1.1 min, but the copy-back hung on `scp` of the Shipping exe, the worker then timed out on ssh and its lock stayed held, so the final DLLs are a local build).
- `Scripts/test.ps1 -Filter CodexTactics.AI.PmcArena`: PASS, 24 tests, 0 failed (incl. `CodexTactics.AI.PmcArenaBehaviour.{PositionTime,PositionContact,OpeningGeometry}`).
- Smokes (`smoke_parallel.ps1 -Remote off`): PmcArenaTacticalSmoke PASS 75.6 s, PmcRoofDefenceSmoke PASS 397.5 s, PmcStalemateSmoke PASS 390.3 s, PmcArenaSmoke PASS 378.8 s (parallel with 3 jobs on a busy machine); PmcCrossroadsSmoke and PmcAlleySmoke FAIL on the listed checks = KNOWN-FAIL. Log: `Saved/Logs/Smoke-Pmc{Crossroads,Alley}Smoke.log`, run log `%TEMP%/t04-run6.log`.
- `git diff --check`, `check_smoke_names.py`: see the final message.

## Notes for the lead
- Codex's own session (`codex.exe`, `test.ps1 -Smart`, ~25 smokes) was still running in this worktree from 10:06 and held the shared build lock until ~12:00; my first build waited ~100 min behind it.
- Next: when T-02 / T-03 merge, switch `map` / `extra` of the two smokes and add the scripted opponent; add PmcBridgeSmoke (A-00C) with the obstacle-top check; T-08 turns these green.
