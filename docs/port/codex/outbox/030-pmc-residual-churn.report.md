# 030 — Residual PMC churn and quiet spells

Status: diagnosis complete; report only — **needs Claude: completed-order policy, heal approach lifecycle, and re-engagement/firing readiness review**. Branch `codex/030-pmc-residual`; agents worktree only.
Routing: level 2 (confidence 0.40) | gpt-6.1-sol | pass (bounded diagnosis completed; game failures remain).

Scope: commander decision code only. No smoke limits, protected movement/combat paths, assets, maps, Config, or main checkout modified.

## Preserved task028 evidence

All nine logs were examined for result/assertion/arena pointers. Six PMC tracks were copied into `Saved/Logs/task030/PmcStalemateSmoke-N-old.track.json`; the three RetreatFacing logs have no PMC arena track and their facing failures are outside this brief. `Saved/Logs/task030/028-manifest.json` records SHA256 and failures for all nine engine logs. `028-repeats.csv` contains all 64 same-order repeats recovered from the tracks. Counts exactly match task028 smoke counters (2, 17, 0, 9, 1, 35).

Scores below are the existing emitted **top score**, not necessarily the chosen order score: PmcSquadSubsystem sends `Scored[0].Score` after failure retries. Reasons are the recorded tactic reasons. Teams disambiguate repeated display names. Times are arena seconds; each listed time is one repeat of that immediately preceding same fighter/order within 2 seconds.

| Run | Team / fighter | Order | Reason | Emitted score | Repeat times (s) |
|---|---|---|---|---:|---|
| 1 | 1 / Operator Kessler | Reload | best | 0.8 | 27 |
| 1 | 2 / Operator Vance | FallBack | best | 0.54 | 66.5 |
| 2 | 1 / Operator Roux | Suppress | best | 0.8 | 10.5, 102.5, 103, 103.5 |
| 2 | 2 / Operator Vance | FallBack | best | 0.54 | 18.5, 20, 21.5, 23, 55, 56.5, 58, 73, 93.5, 110 |
| 2 | 1 / Operator Kessler | Advance | best | 1.12 | 26 |
| 2 | 2 / Operator Vance | FallBack | best | 0.91 | 48.5 |
| 2 | 2 / Operator Vance | FallBack | best | 0.99 | 102 |
| 4 | 2 / Operator Lind | FallBack | best | 0.54 | 35, 36.5, 38, 42.5, 60.5, 74.5, 76, 92 |
| 4 | 1 / Operator Kessler | Advance | invalid | 0.75 | 128 |
| 5 | 1 / Operator Kessler | TakeCover | invalid | 1 | 31.5 |
| 6 | 2 / Operator Lind | FallBack | best | 0.54 | 35, 36.5, 38, 42.5, 60.5, 62, 101, 102.5, 114.5, 125.5, 127, 128.5, 157, 174.5 |
| 6 | 2 / Operator Vance | FallBack | best | 0.54 | 94, 95.5, 99.5, 101, 109, 110.5, 115, 116.5, 129.5, 131, 135, 136.5, 147.5, 149, 150.5, 158, 159.5, 161, 166, 167.5, 172 |

Task028 quiet failures use enemy-targeted shots (`dist_m` in the track), plus rocket bursts near an enemy, matching the smoke. Suppress shots with no enemy actor are excluded. Thus the failure intervals are run4 **125.6→180.0 =54.4 s**, and run5 **53.1→125.6 =72.5 s** (also 127.6→180.0 =52.4 s). Looking at every shot without the target filter understates these gaps.

Run4 at 125 s: A Kessler (-878,-5201) Objective, A Vance (-2093,-8074) Hold/cover, A Roux (-1817,-7513) TakeCover; B's sole survivor Lind (-514,-3557) FallBack. At 155 s Lind is (2281,-5905), still FallBack. His first Objective is t163, then at 175 s (1702,-8295). A Kessler alternates Objective/Advance, A Vance and Roux hold near (-21m,-80m). Objective is (-2441,-7845), radius 600 cm. Run5 at 53 s: A Kessler 7 HP FallBack, A Vance 36 HP Heal, B Lind 4 HP FallBack; at 93 s A Kessler Hold, Vance Heal, Lind FallBack. Team A heals at 106.1/106.7; enemy-targeted fire returns only at125.6. Old tracks contain positions/orders/HP, but no per-fighter LOS or decision snapshots; those require the new evidence below.

## New evidence

Baseline HEAD: `26b50dfde65972f107dcb3d2900024b92cb7414f`.

Instrumented build PASS (UBT 78.67 s after waiting for the external Build.bat lock). Exactly three independent serial attempts were needed to obtain two FAIL traces. No wrapper retry, no changed limits, no fourth attempt. Each used:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands CodexTactics.PmcStalemateSmoke -NoRetry -Jobs 1 -ExtraArgs '-Multiprocess' -PreCmds 'Codex.Pmc.DecisionTrace030 1' -ResultJson Saved/Logs/task030/PmcStalemateSmoke-N-result.json
```

Every shell inherited `CODEX_AGENT=codex`, `PYTHONIOENCODING=utf-8`. Standard shared locks and designer priority stayed in force. Full engine/wrapper/result/track/arena files were preserved immediately after each attempt under `Saved/Logs/task030/PmcStalemateSmoke-N*`; `030-manifest.json` records engine hashes and `030-repeats.csv` the exact new repeats. The temporary trace patch is preserved only as ignored evidence `temporary-trace.patch`.

| Attempt | Actual engine result | Wrapper exit | Wrapper seconds | Longest quiet | Same-order / limit |
|---|---|---:|---:|---:|---:|
| 1 | FAIL: quiet | 1 | 110.8 | 47.0 s | 3 / 15 |
| 2 | PASS | 0 | 79.6 | 9.9 s | 5 / 14 |
| 3 | FAIL: repeats | 1 | 63.2 | 35.1 s | 10 / 9 |

The dynamic limits for attempts1/2 are verified from their engine assertion lines; engine RESULT, not wrapper exit alone, determines the outcome. New repeat table gives **actual chosen score / existing emitted top score**. The temporary trace captures exact world times; arena time = world time -0.633 s for these three runs.

| Run | Arena time | Delta | Team / fighter | Order | Reason | Chosen / emitted |
|---|---:|---:|---|---|---|---|
| 1 | 24.5 | 0.5 | 2 / Operator Lind | Objective | invalid | 0.253 / 0.253 |
| 1 | 44 | 0.5 | 1 / Operator Vance | TakeCover | best | 0.412 / 1.128 |
| 1 | 127.5 | 1.5 | 1 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 2 | 77 | 0.5 | 1 / Operator Vance | TakeCover | best | 0.923 / 0.923 |
| 2 | 77.5 | 0.5 | 1 / Operator Vance | TakeCover | best | 0.878 / 0.878 |
| 2 | 78 | 0.5 | 1 / Operator Vance | TakeCover | best | 0.839 / 0.839 |
| 2 | 78.5 | 0.5 | 1 / Operator Vance | TakeCover | best | 0.839 / 0.839 |
| 2 | 79 | 0.5 | 1 / Operator Vance | TakeCover | best | 0.839 / 0.839 |
| 3 | 23 | 1.5 | 2 / Operator Lind | Flank | best | 0.380 / 0.380 |
| 3 | 41 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 42.5 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 53.5 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 58.5 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 65 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 73.5 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 84.5 | 0.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 86 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |
| 3 | 87.5 | 1.5 | 2 / Operator Vance | FallBack | best | 0.540 / 0.540 |

## Proven decision causes and scope boundary

1. **Successful retreat completion bypasses commitment and failure cooldown.** `Source/CodexTactics/Private/AI/PmcCommanderRules.cpp:176` gives an outnumbered fighter `fall_back 0.9 × 0.6 =0.54`, including remembered threats while team contact remains recent. The commitment/minimum hold at `:305` / `:311` applies only when `bRunning`; it does not prevent repeating a completed order. In `PmcSquadSubsystem.cpp:1092`, only a walk ending more than400 cm short fails; no cooldown is set for arriving. `:1654` issues another800 cm FallBack (`PmcCommanderRules.h:514`, `PmcCommanderRules.cpp:452`). This is distinct from024's failed-cooldown overwrite.

   Fresh run3 engine line2474: world41.633, B Vance, FallBack age1.5 s, running0, move0/pending0, feet(-3945.50,-4822.55,2.15), previous goal(-3952.65,-4799.97,10), planar residual23.68 cm; cooldown0. His alive1/known2 view scores FallBack0.540, Crouch0.500, TakeCover0.489, Objective0.343. Line2476 confirms the next FallBack is taken. At world43.133 the same repeats with residual23.69 cm and score0.54 despite fireline1 and threat3783 cm inside6000 cm range. The 3 s minimum hold does not cover these completed1.5 s legs. At world65.633 he is(-5067.51,439.47), 91.32 m from the remembered threat, still FallBack0.54 > Objective0.403; at world75.633 quiet30.417, Objective1.379 finally beats FallBack. A completion policy is needed, rather than widening the smoke threshold or reapplying a failure cooldown to successful arrival without design review.

2. **Quiet push occurs on schedule, but cannot guarantee contact within the remaining15 s.** `PmcCommanderRules.cpp:279` / `:281` use configured `push_after_s 30`; `PmcSquadSubsystem.cpp:1286` derives quiet time from the last shot. The new quiet failure spans arena50.9→97.9 =47.0 s. The temporary trace logged all scores, cooldowns, goals, positions, targets, team-seen state, and useful muzzle fire line at each decision. Selected snapshots below use **world** seconds and feet in cm. `seen` means shared team sight (`PmcSquadSubsystem.cpp:1180`), and fireline is `HasUsefulFireLine` (`:1186`); neither alone proves the fighter's complete firing readiness.

| World / quiet | Fighter | Order / activity | Feet | Threat actor / actual feet-plan position | Seen / fireline |
|---|---|---|---|---|---|
| 61.633 /10.05 | A Vance | Hold, cover0.997, stationary | (-6100,-3104) | C_6 at(1446,-5765), belief(-463,-3521) | 0 /0 |
| 61.633 /10.05 | B Lind | Objective, moving | (1446,-5765) | C_4 at(-6100,-3104), belief(-6658,-3579) | 0 /0 |
| 61.633 /10.05 | B Vance | Heal, moving to patient's old goal | (-1579,-6400) | C_4 at(-6100,-3104) | 0 /0 |
| 71.633 /20.05 | B Vance | Heal still running; move0/pending0 | (1453,-5670) | C_4 at(-6100,-3104), belief(-6658,-3579) | 0 /0 |
| 81.633 /30.05 | A Vance | Hold→Objective0.734 | (-6100,-3104) | C_6 at(1439,-8596), belief(-463,-3521) | 0 /0 |
| 81.633 /30.05 | B Vance | Heal→Objective1.858; Heal0.770 | (1453,-5670) | C_4 at(-6100,-3104) | 0 /0 |
| 91.633 /40.05 | A Vance | TakeCover→Suppress; FallBack cooldown4 s | (-2339,-7846) | C_8 at(-2133,-7739),232 cm | 1 /1 |
| 91.633 /40.05 | B Vance | Objective invalid→FallBack1.252 | (-2133,-7739) | C_4 at(-2339,-7846),232 cm | 1 /1 |
| 97.633 /46.05 | A Vance | Hold in cover0.680; clip0.8 | (-2118,-7975) | C_8 actual(-2138,-7735), belief(-2032,-7760) | 1 /1 |

   New run1 engine lines2781–2783,2845–2847,2909–2915,2986–2992,3051–3053 hold these snapshots. Before the push the sole A survivor holds roughly60 m from the point; B Lind walks a long route around its east/south perimeter; B Vance stops at the heal approach goal. At quiet30.05 the push works for both sides, but ten seconds later it meets at232 cm and changes orders again. Counted enemy fire still does not resume until47.0 s. Decision snapshots alone cannot identify the remaining facing/cover/target/weapon readiness gate; protected firing files were not read deeply or edited. **Needs Claude: correlate these snapshots with the firing pipeline and personal sight/pose readiness.** A completed-order cooldown alone cannot establish that the quiet failure is fixed.

3. **Heal approach stays running after its move ends.** `PmcSquadSubsystem.cpp:989` / `:990` calls Heal running while its patient is live/not full and a medkit exists, independently of the approach move. `:1399` then skips re-executing the same running order. `:1658` starts Heal by walking to the patient's then-position if not nearby; `:1246` can refresh the patient binding during later views. In run1 B Vance has Heal age10.5 s and move0/pending0 at world71.633, goal(1445,-5693), while patient Lind has already moved to(1462,-7195), ~15.25 m away. Vance remains at(1453,-5670) until the push preempts at81.633. This is a proven stale approach/lifecycle contribution, not evidence that treatment fired or that fixing Heal alone fixes the smoke. **Needs Claude: implement and validate heal approach/arrival/retarget completion across decision and execution lifecycles.**

4. **Top-score telemetry masks retry choices.** `PmcSquadSubsystem.cpp:1425` / `:1427` emits `Scored[0].Score`, including after a refusal retry. Run1's TakeCover repeat at arena44 s actually chooses0.412 while emitting1.128. Old028 values cannot be relabeled as chosen scores. No permanent telemetry change was mixed into this diagnostic task.

5. **The emitted-order sequence omits a sight-memory takeover.** `PmcSquadSubsystem.cpp:800` / `:805` may start point suppression and replace `FighterState.Order` with Suppress after a recently fired-at target is no longer team-seen. It neither resets OrderSince nor emits OnFighterOrder there. Fresh run3 engine2145 emits Flank at world22.133;2162/2182 read current=Suppress, ages0.5/1.0, move0, with the old flank goal still(-4571,-7890);2196 then reads Suppress not running and2199 emits another Flank at23.633. The smoke sees Flank twice1.5 s apart while the internal state took an unrecorded Suppress detour. Fresh run1 similarly emits Objective24.633 (line2166), then reads Suppress age0.5/move0/fireline0 and emits Objective again25.133 (2180/2181, reason invalid). Thus these repeats are not all successful arrivals. **Needs Claude: reconcile lost-sight suppression with commander ownership, commitment/goal state and order telemetry, preserving the intended honest-fire behavior.** No firing-memory logic was changed.

6. **A short interrupted cover approach can look completed to the commander.** Fresh run2 engine3158 issues TakeCover at77.133;3164 logs `[Sniper] Operator Vance stops to take the shot`;3166 then reads TakeCover age0.5/running0/move0/pending0/cover0, still at(-5194.93,-5801.92) with goal(-5341.75,-5700). The179 cm residual is inside the400 cm failed-walk test (`PmcSquadSubsystem.cpp:1092`), so no failure cooldown applies;3169 repeats TakeCover0.923. Five repeats occur every0.5 s in the successful run. A broad400 cm spatial tolerance is not proof that the cover transition actually completed. **Needs Claude: classify cover arrival versus shot interruption and preserve cover approach through sniper stop behavior.** The sniper/firing implementation was not inspected or edited. The remaining old028 Reload/Advance/Suppress repeats have only legacy event/log evidence, not retrospective decision snapshots; their individual runtime cancellation mechanisms remain unproven and are included in the escalation rather than guessed.

The commander-side repeat cause is local, but both requested failure classes include lifecycle, long route, and firing-readiness interactions. No proven small commander-only fix covers them. Report-only escalation is therefore the brief's appropriate branch. No JSON values or unit tests were changed; no six-run fix validation or companion guards were run because there is no fix to validate. Smoke assertions/limits and knownFlaky flags remain unchanged.

## Cleanup and verification

Temporary instrumentation was exactly26 added lines in `PmcSquadSubsystem.cpp`, gated behind `Codex.Pmc.DecisionTrace030` (default0), with no behavior change. It was removed using an explicit-path restore after preserving all three traces. Source diff is empty; `git diff --check` PASS and no DecisionTrace030/Pmc030 string remains in the decision source. Final original-binary rebuild: PASS, exit0, UBT56.79 s after the normal external Build.bat wait. Build evidence: `Saved/Logs/task030/Build-instrumented.log`, `Build-restored.log`.

Only this outbox report is intended for commit. Parent reviewed the report, verified the empty source diff and ran the required final audit before committing this report. No push, merge, main write, protected-path edit, local model, or agent chat message was performed by this subtask.
