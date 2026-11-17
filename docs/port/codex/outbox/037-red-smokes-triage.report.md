# 037 red smokes triage

Status: IN PROGRESS. StealthModeSmoke and FormationJitterSmoke need Claude; CoverHugSmoke fixed df1db2f3/gated; MantleDescentSmoke merged-base recheck PASS (no local fix). Next/final: LevelGenRampSmoke. PMC pair Gemini077; corpse cost Gemini078.

## Current summary

| Smoke | Verdict / commit | Gate |
|---|---|---|
| RetreatFacingSmoke | Green latest-base recheck; report8a3ecc24 | once PASS39.6s; dropped, flags unchanged |
| CoverSmoke | Green latest-base recheck; report8a3ecc24 | once PASS42.5s; dropped, flags unchanged |
| MilitaryZombieSmoke | Stale montage test fixed11b04070; accepted Claude#604 | target3/3, Smart18units+4smokes PASS; initial Corpse finding addressed separately |
| CorpsePersistSmoke | Stale mesh-origin proxy fixedb3fafd28 | baselineP/F/P, target3/3+isolated PASS; Smart5units+5smokes PASS, startup timeout recovered, separate CorpseCost FAIL |
| OptimizedCharacterSmoke | Stale montage test fixed4616ea40 | target3/3, Smart1smoke(no units selected), four related smokes PASS |
| CorpseCostSmoke | needs Claude; budget unchanged | two actual FAILs:0.92ms /1.70ms vs0.5ms |

Pending original order: StealthModeSmoke, CoverHugSmoke, MantleDescentSmoke, FormationJitterSmoke, LevelGenRampSmoke, then diagnosis-only RpgPmcSmoke and PmcEncounterHuntSmoke.

## Scope and baseline

- Worktree: `C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics-agents`.
- Branch: `codex/037-red-smokes`; baseline HEAD `01efefe9b8ea60c3c2210229945f06eac0972098` (assigned latest baseline containing Claude 074/075 and Codex 036).
- Read inbox `037-red-smokes-triage.md`, Codex README, relevant HANDOFF/CLAUDE rules and build/lock protocol.
- Per this work block's instruction, used the already prepared baseline: no fetch, pull, branch switch, merge, or main-checkout writes.
- Step0 made no source/flag/runtime/config/map/asset/Depot edits and committed only this report. Later test-only source edits are listed below. Existing untracked asset/config directories remain untouched.

## Router and model

| Sub-part | Level | Model used | Result |
|---|---|---|---|
| Step 0: baseline build, two single smoke runs and evidence/report | 2 | gpt-6.1-sol (delegated cheaper worker) | pass; no redo |

The parent dispatch recorded level 2 / confidence 0.39. The worker's required fresh classification also returned level 2, confidence 0.58, `route: false` because confidence was below 0.60. The classifier's Claude model name is ignored per the mailbox README; the explicit instruction to use the cheaper model for levels 1–2 governed execution. Full fresh output: `Saved/Logs/task037/step0/router.log`.

## Build

Command: `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1` (standard defaults; no designer-priority override).

- Started `2026-11-17T05:37:18.3535997+02:00`; finished `2026-11-17T05:47:52.4713448+02:00`.
- Engine result: `Result: Succeeded`; engine execution time **614.88 seconds**; wrapper exit **0**.
- 52 build actions completed. Engine-header deprecation and compiler-version warnings were present; no build failure.
- Full evidence copied immediately after completion: `Saved/Logs/task037/step0/build-engine.log` (113024 bytes), `build-wrapper.log`, `build-result.json`.

## Single-run smoke results

Both commands used `Scripts/smoke_parallel.ps1 -Commands CodexTactics.<Name> -Jobs 1 -NoRetry -ExtraArgs '-Multiprocess' -ResultJson Saved/Logs/task037/step0/<Name>-result.json`. They ran serially, exactly once each, on the wrapper's default fixed 60 fps step and fixed seed. No retry or additional run was performed.

| Smoke | Actual engine result | Duration | Wrapper exit | Step 0 disposition |
|---|---|---|---|---|
| RetreatFacingSmoke | `Smoke RESULT: PASS` | 39.6 s (wrapper wall 41.2 s) | 0 | Green: dropped from active 037 list |
| CoverSmoke | `Smoke RESULT: PASS` | 42.5 s (wrapper wall 43.6 s) | 0 | Green: dropped from active 037 list |

- RetreatFacingSmoke started `2026-11-17T05:48:16.9069549+02:00`, finished `2026-11-17T05:48:58.7286778+02:00`. Engine PASS at preserved log line 1633, engine timestamp `2026.11.17-03.48.56:891`.
- CoverSmoke started `2026-11-17T05:49:15.9856498+02:00`, finished `2026-11-17T05:50:00.1089063+02:00`. Engine PASS at preserved log line 1821, engine timestamp `2026.11.17-03.49.57:907`.
- Each full engine log, wrapper log, result JSON and run metadata was preserved immediately upon that run's completion under `Saved/Logs/task037/step0/`, named `<Name>-engine.log`, `<Name>-wrapper.log`, `<Name>-result.json`, `<Name>-run.json`.
- Actual engine PASS markers above determine the results; a known-fail wrapper exit was not used as evidence. Both result JSON files report `State: PASS`, `jobs: 1`, and empty `rerunPassed` arrays.
- No unit tests or later triage gates were run: this block was restricted to step 0. No code fix was needed; parent commits this step as a report-only checkpoint.

## Next

Step0 was committed as `8a3ecc24`. Subsequent per-smoke results follow; current pending order is in the summary and at the end.


## MilitaryZombieSmoke — stale montage assertion; initial related-gate findings

- Source commit: `11b04070` (`test(smoke): update MilitaryZombieSmoke for retained ragdoll death`). Only MilitaryZombieSmokeCommand.cpp changed: 18 insertions / 7 deletions including comments/include. No runtime, flags, timings, maps or assets changed.
- Cause: Task 056 commit `918d95d3` deliberately makes EnemyAnimInstance skip death montage while IsRagdollActive; current `EnemyAnimInstance.cpp:171`, `EnemyCharacterHealth.cpp:226` activate/reflect that contract. Task005 specifies default immediate ragdoll, settling/freeze and retained bodies; 067 `d734045e` supplies current floor/settle semantics. Old smoke demanded Death_ montage and one-shot weight >0.9 after death. Baseline Military engine FAIL despite ragdoll activation/freeze in1.48s and pelvis body resting at floor.
- Replacement positively requires dying + kept corpse + active ragdoll + simulating/frozen mesh + floor traced under resolved pelvis body, gap in [-configured max sink,45cm), matching the existing DeathRagdollSmoke body-bottom acceptance. Stage4.5s timing and every other assertion remain. This changes the expected death mechanism, not the game or assertion limits.
- Public API/contract references: EnemyCharacter.h IsRagdollActive/IsDying/IsCorpseFrozen; CorpseSubsystem.h ResolvePelvisBone/GetPelvisBodyBottomZ/TraceCorpseFloorZ/IsKept; existing DeathRagdollSmokeCommand.cpp:357-377.
- Baseline: MilitaryZombieSmoke FAIL46.4s; OptimizedCharacterSmoke FAIL63.5s (Marksman and Hound montage assertions after ragdoll activation). Preserved as Saved/Logs/task037/death/<Smoke>-baseline-*; no Optimized source edit yet.
- Router pair: level2, confidence0.52 | gpt-6.1-sol bounded diagnosis and patch proposal; parent review/application/gates | targeted pass; related gate blocked. No local model.

| Gate | Result / evidence |
|---|---|
| Incremental build | PASS31.09s, wrapper exit0; MilitaryZombie-fix-build-* |
| Required standalone1/2/3 | PASS43.2s / PASS42.9s / PASS42.4s; Jobs1, NoRetry, Multiprocess; MilitaryZombie-fixed-N-* |
| Smart plan | actual anim-enemy rule, EnemyAI+EnemyAnim+EnemyAssets, four mapped smokes; 4023 changed/untracked paths,4022 nonverification |
| test.ps1 -Smart -Jobs1 | 18/18 unit tests PASS; MilitaryZombie41.6s, EnemyHitLayer37.5s, Marksman37.1s, EnemyDeath39.1s all PASS, no retry; MilitaryZombie-smart-* |
| DeathRagdollSmoke | PASS, standalone Jobs1/NoRetry/Multiprocess; MilitaryZombie-related-DeathRagdollSmoke-* |
| KnockdownSmoke | PASS42.5s, standalone Jobs1/NoRetry/Multiprocess; MilitaryZombie-related-KnockdownSmoke-* |
| CorpsePersistSmoke | FAIL41.5s, standalone Jobs1/NoRetry/Multiprocess; related gate is NOT green; MilitaryZombie-related-CorpsePersistSmoke-* |
| Staged source audit / whitespace | PASS, risk1.3/4; explicit single smoke path |

All evidence prefixes above are under Saved/Logs/task037/death/. Full engine/wrapper/result files are retained, including Smart unit log and copied structured Smart test report. PASS is read from actual engine RESULT markers.

CorpsePersistSmoke first failure:
```text
Smoke FAIL: hound (BP_Enemy_Hound_C): stays 30 s, frozen 1 (controller None, ticks actor 0 / mesh 0 / move 0), collision off 1, visible 1, out of the registry 1, on the ground (root 61 cm), death pose weight 0.00
```
**needs Claude: determine stale CorpsePersist expectation versus an actual corpse-pose regression; this related gate failed and the protected Corpse/ragdoll implementation was not changed or investigated further.** Military's specific target passes3/3 and Smart passes, but its broader related gate remains blocked. No retry was used to hide this failure.

Execution/approval note: a worker source-edit/build request was denied before any execution for missing explicit task authorization. Parent supplied the original user's direct delegation of NEXT tasks from Claude and obtained approval for the narrow test-only edit and gates. One later review-service Bad Request executed no test; the three recorded standalone runs are the three actual invocations.

Claude review #604 accepted `11b04070`: another smoke cannot execute this test-only change, so CorpsePersist was added as a separate red rather than blocking Military. Its baseline comparison and fix follow. OptimizedCharacterSmoke follow-up is recorded below.


## CorpsePersistSmoke — separate red from Claude review #604

Source fix: `b3fafd28` (`test(smoke): measure CorpsePersistSmoke ragdoll floor contact`). Router: level3, confidence0.56 | primary Codex main session | targeted pass; broader Smart findings remain explicit.

### Requested baseline, without Military change

Tracked state was clean. The only code difference from `01efefe9` was MilitaryZombieSmokeCommand.cpp. Temporarily restored that one file from `01efefe9`, verified `git diff 01efefe9 --exit-code -- Source Content Config Scripts` empty, rebuilt (PASS11.03s), then ran CorpsePersist three times alone with Jobs1/NoRetry/Multiprocess. Results: **PASS / FAIL / PASS**. Every engine/wrapper/result/run record is under `Saved/Logs/task037/corpse-baseline/CorpsePersist-N-*`; baseline build logs are in the same directory. A finally block restored committed Military source from `11b04070`; the subsequent32.61s build compiled restored Military plus the new Corpse smoke fix. No main checkout or branch writes.

Baseline run2 first FAIL was the same hound at mesh origin61cm, with all frozen/controller/tick/collision/visibility/registry flags correct. Its ragdoll freeze logs show pelvis bone15.3cm, physical body bottom approximately0cm and floor0cm. This reproduces without Military and disproves a Military-induced failure.

### Cause and replacement

Important correction: `death pose weight0.00` is diagnostic text, NOT an asserted condition. `RootOverGround()` actually measures **GetMesh()->GetComponentLocation()**, not a root bone or collision contact. The old condition `abs(Ground)<60` came from pre-ragdoll `60206b87` (2026-11-09), before the retained ragdoll contract `918d95d3`/067. A correctly lying hound can leave that mesh-component origin61cm up.

Only CorpsePersistSmokeCommand.cpp changed:25 insertions/3 deletions. For active ragdolls the smoke now uses existing DeathRagdoll acceptance: resolved pelvis, physical body-floor trace, scale-normalized pelvis height<45cm, body-bottom gap in[-configured max sink,45cm). Legacy non-ragdoll bodies keep the original60cm test. Every30s retention/freeze/controller/tick/collision/visibility/registry/cap/fade/nearest-body assertion is preserved. No game code, flags, configuration, maps, assets or performance budget changed.

A fixed-run PASS proves the former failure geometry was exercised: hound mesh origin61cm, physical floor found1, body gap-0.0cm, pelvis19.1 skeleton cm. Thus this was not merely three favorable legacy-origin samples.

| Gate | Actual result |
|---|---|
| Final build | PASS32.61s; includes restored Military source |
| Required standalone1/2/3 | PASS53.0s / PASS49.5s / PASS49.5s, each Jobs1/NoRetry/Multiprocess |
| Smart units | 5/5 PASS, filter CodexTactics.Characters.Corpse |
| Smart related initial runs | EnemyDeath143.9s, CorpseFall41.9s, Knockdown46.1s, MilitaryZombie44.4s, LadderEnemy45.6s PASS; CorpsePersist startup TIMEOUT500.1s; CorpseCost FAIL113.8s |
| CorpsePersist isolated Smart-equivalent recheck | PASS43.6s; Jobs1/NoRetry, no Multiprocess, process-local UE_SKIP_UBT_SDK_SETUP=1 to suppress only startup UBT validation |
| CorpseCost isolated recheck | FAIL116.6s; same process-local SDK skip; budget unchanged |
| Extra DeathRagdollSmoke | PASS42.0s, standalone Jobs1/NoRetry/Multiprocess |
| Source audit / whitespace | PASS, risk0.4/4; exactly the single smoke source staged |

Evidence: `Saved/Logs/task037/corpse-fix/`: build-*, CorpsePersist-fixed-N-*, smart-plan.log, smart-wrapper.log, smart-unit-engine.log, smart-test-report/, smart-<Smoke>-engine.log, isolated-<Smoke>-*, related-DeathRagdollSmoke-*. Actual engine RESULT markers determine game PASS/FAIL. Full reports/logs preserved.

Smart itself exited1; it is **not reported as seven green smokes**. CorpsePersist's timed-out process never reached a Smoke RESULT; its last log is UBT `-Mode=ValidatePlatforms -OutputSDKs` at04:54:47 UTC, matching029 startup contention. Safe ownership checks found the processes had advanced/exited, so no cancellation occurred. The original Smart continued. Only the later two isolated child launches used process-local UE_SKIP_UBT_SDK_SETUP=1 (source-backed029 control); it was not persisted and no designer-priority override was used.

CorpseCost first actual assertion:
```text
Smoke FAIL: 100 more frozen bodies (50 -> 150) add 0.92 ms of game-thread time (budget 0.5 ms)
```
Its one isolated recheck also failed: **1.70ms vs0.5ms**. **needs Claude: independent frozen-body game-thread cost investigation; do not widen the0.5ms budget.** CorpsePersist source is not executed by CorpseCost; protected Corpse/ragdoll runtime was not edited or investigated deeply.

At the Corpse checkpoint, OptimizedCharacterSmoke was the next authorized item. Its completed follow-up is below.


## OptimizedCharacterSmoke — retained ragdoll replaces stale montage check

Source commit: `4616ea40` (`test(smoke): recognize OptimizedCharacterSmoke retained ragdoll death`). Router: level3, confidence0.58 | primary Codex main session | pass, no redo. Only OptimizedCharacterSmokeCommand.cpp changed,16 insertions/7 deletions including comments/include.

The unchanged baseline on01efefe9 already failed63.5s on Marksman and Hound death-montage expectations despite ragdoll activation (Saved/Logs/task037/death/OptimizedCharacterSmoke-baseline-*). Cause is the same deliberate918d95d3 contract: active ragdoll suppresses the montage. CheckDeath runs after the existing90-tick/frame delay; a body can still be simulating then, so requiring an already settled floor pose here would be a different stale timing assumption.

For active ragdolls the optimized body's death visual must now be registered/kept by CorpseSubsystem and actually simulating physics or frozen. If ragdoll is inactive, the old HasDeathAnimation/montage fallback remains unchanged. Both bodies must still exist and be dying, optimized anim bIsDead stays required, and every later pose/bone, material, LOD, skeleton, physics-asset and variant assertion stays unchanged. No timing, gameplay/runtime, flags, assets or limit changes.

Fixed-run evidence explicitly exercises both prior failures: Marksman and Hound report `death clip NOT playing`, `retained ragdoll1`, `simulating1`, `frozen0` and PASS. Thus the replacement checks actual physics rather than merely accepting the ragdoll flag or assuming frozen state.

| Gate | Actual result |
|---|---|
| Build | PASS38.66s |
| Required standalone1/2/3 | PASS74.7s / PASS76.0s / PASS71.5s; each Jobs1/NoRetry/Multiprocess |
| Actual Smart plan | optimized-character rule; no unit filter; only OptimizedCharacterSmoke selected |
| test.ps1 -Smart -Jobs1 | PASS56.2s, one smoke, no retry;0 units selected, not a unit-suite PASS claim |
| Related EnemyDeathSmoke | PASS39.9s, once Jobs1/NoRetry/Multiprocess |
| Related MilitaryZombieSmoke | PASS45.8s, same flags |
| Related CorpsePersistSmoke | PASS43.7s, same flags |
| Related DeathRagdollSmoke | PASS42.3s, same flags |
| Staged source audit / whitespace | PASS, risk1.1/4; one explicit smoke path |

All evidence: `Saved/Logs/task037/optimized/`: build-*, OptimizedCharacter-fixed-N-*, smart-plan.log, smart-wrapper.log, smart-result.json, smart-OptimizedCharacterSmoke-engine.log, related-<Smoke>-*. Engine RESULT markers verify each PASS. Smart's child launch alone used process-local UE_SKIP_UBT_SDK_SETUP=1 after the proven startup contention; no persistent configuration or designer-priority override. No stale unit report was copied because Smart selected no units.

Independent CorpseCost performance failures above remain needs Claude; no extra performance rerun or budget change was made during this smoke's gate.

Next: **StealthModeSmoke**. Remaining original order after that: CoverHugSmoke, MantleDescentSmoke, FormationJitterSmoke, LevelGenRampSmoke; then diagnosis-only RpgPmcSmoke and PmcEncounterHuntSmoke. Protected runtime areas remain off limits. No push/merge/main checkout writes.


## Designer stop / new-session handoff

Designer explicitly requested all Codex processes stopped to continue in a new session. Codex chat listener stopped; owned pending diagnostic-build tree5816/57944/65380 stopped. Other agents/designer processes and the pre-existing Training Ground server were not targeted. No new wait loop or build is started after this handoff.

Permanent smoke-only fixes remain on branch codex/037-red-smokes: MilitaryZombie11b04070, CorpsePersistb3fafd28, OptimizedCharacter4616ea40. Their gates and the independent CorpseCost0.92/1.70ms vs0.5ms needs-Claude finding are above. Last complete report before stop3cfe9463.

### StealthModeSmoke partial findings (no fix)

- Routerlevel3/confidence0.60, primary Codex. Current baseline once Jobs1/NoRetry/Multiprocess: FAIL96.0s. Evidence Saved/Logs/task037/stealth/baseline-*.
- QuickFire order accepted at06:08:09.467UTC/frame344; at06:08:09.527/frame380 existing log says no line of fire and burst ends. The combined actual-fired/stealth assertion fails at06:08:10.077/frame724, followed by original-material assertion. No claim is made that stealth callback itself is faulty: the existing message does not distinguish ammo/state.
- Bounded reads only: exact smoke phase50/6, StealthModeRules ShouldBreak, OperativeCharacterStealth HandleStealthWeaponFired. Protected QuickFire/Aim/Combat code untouched.
- A temporary message-only diagnostic preserved the exact assertion and8s deadline, intending to print ammo/stealth/stage/enemy state. First diagnostic build failed44.54s because of my unverified IsInCover getter; its error logs were preserved. Removed that optional field. Second diagnostic build waited at the shared Build.bat lock and was stopped before compilation when designer requested stop. No diagnostic smoke run occurred.
- Temporary source instrumentation has been restored exactly from HEAD; Source diff is empty. Temporary patch remains only in ignored Saved/Logs/task037/stealth/temporary-diagnostic.patch. Build1 failed before link, build2 was still waiting before compilation; last successful binaries remain the already-gated permanent fixes. No further rebuild was launched after stop.

Resume: stay in agents worktree on codex/037-red-smokes; read this report and inbox037. Next decision is whether to finish the passive Stealth diagnostic or hand the no-line-of-fire precondition to Claude. Do not weaken assertions or change protected firing code. After Stealth: CoverHug, MantleDescent, FormationJitter, LevelGenRamp, then diagnosis-only RpgPmc/PmcEncounterHunt. Honor the original protected-file and10% rules. Do not merge/push/main-write.


## Resumed session / StealthModeSmoke — needs Claude

- Designer stopped the old chat and assigned this chat ownership; Claude confirmed in chat611. Preserved every earlier commit and the old session's staged handoff above. Saved the complete uncommitted diagnostic patch and original source snapshot in Saved/Logs/task037/resume/. Old session restored its diagnostic-only source before stopping; no source changes remain from it. Its failed diagnostic build used nonexistent IsInCover; that temporary variant is preserved in Saved/Logs/task037/stealth/temporary-diagnostic.patch, not committed as working code.
- Router: level3, confidence0.53 | primary main session | diagnosis complete, no fix claimed. No local model.
- Fresh unchanged-source recheck: actual FAIL103.5s, Jobs1/NoRetry/Multiprocess/fixed60fps, with diagnostic-only PreCmds Codex.Debug.FireTrace1. Full engine/wrapper/result: Saved/Logs/task037/stealth/resumed-baseline-*. Prior stopped-session baseline FAIL96s also retained.
- Exact failure chain: quick-fire order accepted; CanFireAt reports cover pose out of reach; RunQuickFire steps off the wall; target becomes hidden behind the wall; HasUsefulFireLine rejects the shot; clip remains30; actual-fired-round/stealth-material assertions fail. No shot occurred, so this does not establish a failure of break-on-fire itself.
- Relevant existing contract: 571894f4 introduced the universal fire gate in OperativeCharacterQuickFire.cpp / OperativeCharacterFireGate.cpp. CanFireAt judges the actual cover barrel and returns CoverPoseOutOfReach after the stance settles; the caller then BeginOpenShotFromCover. OperativeAnimInstance.cpp UpdateCoverLayer and PlayCoverMontage deliberately suppress regular cover clips while IsInStealthMode; OperativeAnimInstanceStealth.cpp UpdateStealthLayer yields to aiming only outside cover. This interaction needs a coordinated readiness/pose transition; do not bypass the gate, relocate the fixture to hide the blocked shot, or clear stealth before a real round to satisfy the assertion.
- Verdict: needs Claude. No assertion/expectation/flag/runtime/config/asset change; no claimed three-green or fixed-code gate. No extra build required for this report-only diagnosis (old diagnostic build was cancelled by the stopped session).
- Updated assignment: skip RpgPmcSmoke + PmcEncounterHuntSmoke entirely, Gemini077; independent CorpseCost belongs to Gemini078. The older pending/diagnosis-only wording above is historical.
- Next: merge current main into codex/037-red-smokes, stop and ask in chat on ANY conflict, rebuild, then CoverHugSmoke once on merged base. Continue MantleDescentSmoke, FormationJitterSmoke, LevelGenRampSmoke in that order.


## 10% limit checkpoint — designer ordered immediate stop

- Earlier work and old-session handoff preserved in963485d2; complete pre-cleanup diagnostic patch/source snapshot in Saved/Logs/task037/resume/. Tracked source remained unchanged.
- Merged main46e2f083 into codex/037-red-smokes without conflicts. No main checkout writes/push/merge into main.
- Merged build + one CoverHug baseline are running as a serial shell chain (existing execution session; inspect process/logs rather than starting another build). Build logs Saved/Logs/task037/merged/build-wrapper.log and Build.log; on completion chain copies build-engine.log and runs CoverHugSmoke Jobs1/NoRetry/Multiprocess, preserving CoverHug-baseline-{engine.log,wrapper.log,result.json}. Last observation34/49 build actions; no success or smoke result claimed. Do not duplicate the chain; inspect its outcome next session.
- CoverHug router initial routine classification2; narrowed runtime/nav-slot geometry diagnosis classified4 confidence0.60, primary main model. No implementation or fix gate yet. All assertions unchanged.
- Next: collect build/baseline result; diagnose CoverHug, then MantleDescent, FormationJitter, LevelGenRamp, merging current main/rebuilding/rechecking as instructed. Skip PMC pair (Gemini077) and corpse performance (Gemini078).
- Stop triggered by designer's explicit under10% warning. Reset time is not exposed by this session; unknown, do not invent a timestamp. Only agent_chat wait after this checkpoint.


## CoverHugSmoke — observe the return animation while the shimmy runs

- Main46e2f083 was merged without conflicts as6b460de2. Required merged build PASS659.56s; fresh unchanged-source CoverHug baseline FAIL44.1s. The old035 failure (return order rejected) no longer occurs: after merged F-2 leader-corner reservation0e9c49e7, the return is accepted and reaches the correct corner. The remaining FAIL is the fixed0.8s animation snapshot: the short return has already completed and correctly plays look_at_idle with shimmy direction0.
- Fix is smoke-only, CoverHugSmokeCommand.cpp11 insertions/3 deletions. Sample positively requires both actor and anim shimmy flags, forward shimmy direction and walk_fwd loop; reset per leg. The return assertion requires at least one such active-motion sample and zero backward clips throughout the leg. Existing order acceptance, mesh yaw, endpoint hug, no-fire pose, skin clipping, roof/eaves and all geometry thresholds remain unchanged. The changed timing expectation is justified by the baseline's actual successful arrival before its0.8s observation, not by relaxing the animation contract.
- Router: preliminary routine-only classify2/confidence0.62; runtime/geometry triage classify4/confidence0.60 | primary main session | pass, no redo. No local model.
- Build with final fix PASS46.44s. Three independent standalone Jobs1/NoRetry/Multiprocess/fixed60fps runs PASS52.7s /68.0s /67.6s. Each reports22 forward-moving samples,0 backward; observed return clip becomes look_at_idle after arrival. First fixed run is retained under CoverHug-diagnostic-* (diagnostic helper was removed before compilation; engine output proves final sample assertion executed).
- Smart plan:4023 paths,4022 no-verification; only this smoke selected,0 unit tests selected. Actual test.ps1 -Smart -Jobs1 PASS65.7s, no retry. Process-local UE_SKIP_UBT_SDK_SETUP=1 used for Smart as in previous gates, no persistent config change.
- Related standalone Jobs1/NoRetry/Multiprocess: CoverHugMedicSmoke PASS61.9s; CoverMoveSmoke PASS83.1s; CoverReverseSmoke PASS64.1s; CoverSmoke PASS68.2s. All actual engine RESULT markers verified, no knownFail/knownFlaky changes.
- Evidence: Saved/Logs/task037/merged/build-*; CoverHug-baseline-*; CoverHug-diagnostic-build-* and CoverHug-diagnostic-*; CoverHug-fixed-2/3-*; CoverHug-smart-plan.log / smart-wrapper.log / smart-engine.log; CoverHug-related-<Smoke>-*. Standalone2 waited for designer editor startup; its753s wrapper wall time includes that priority wait (68s actual smoke). Never bypassed priority.
- Source/report audit and whitespace check required before this commit. Only this smoke source and report are staged. Next: merge current main, rebuild, single MantleDescentSmoke baseline. RpgPmcSmoke/PmcEncounterHuntSmoke skipped per designer reassignment to Gemini077; CorpseCost remains Gemini078.


## MantleDescentSmoke — green required merged-base recheck, no local fix

- Mandatory current-main merge completed without conflicts: main0cc08c4b merged as cfd2cdf0. Build PASS802.61s, wrapper0. Normal shared Build.bat mutex queue respected; no process was interrupted.
- Exact required single fresh recheck on that merged base: MantleDescentSmoke PASS89.9s; Jobs1/NoRetry/Multiprocess/fixed60fps; actual engine Smoke RESULT: PASS. Original 230cm parapet top-state and 140cm wreck island fall/no-vault assertions all pass in this run.
- No local source, expectation, timing, configuration or asset changes for this smoke. This is one observed green recheck, not a claim that an intermittent failure was independently fixed or a three-run local-fix gate. Per designer instruction, one recheck was made after merge; no extra retries.
- Router: level3/confidence0.46 | primary main session | green recheck, no fix.
- Evidence Saved/Logs/task037/mantle/: base.txt, build-wrapper.log, build-engine.log, baseline-wrapper.log, baseline-engine.log, baseline-result.json. All preserved after completion.
- Chat routing: original codex-037 rejected the CoverHug post with TOPIC_LIMIT. Designer instructed asking Claude; question619, approval620 authorize codex-037-results. CoverHug's single result line was delivered as621. Remaining per-smoke/final lines use the approved new topic.
- Corrected only a report typo above: CoverHug refined router confidence was0.60 (Mantle classification was0.46).
- Next FormationJitterSmoke: merge current main first, stop/ask on conflicts, rebuild, single baseline on merged base. LevelGenRampSmoke follows; PMC pair skipped (Gemini077).


## Formation gate queue checkpoint — Claude coordination625

- Designer-authorized result topic codex-037-results is active: CoverHug delivered621; MantleDescent delivered623.
- Before FormationJitter, main66318a6b was merged conflict-free (docs only). Required build is still queued at the shared Build.bat mutex since approximately10:29 Jerusalem; no result claimed and no duplicate build launched. Existing shell chain will copy build evidence and then run one FormationJitterSmoke Jobs1/NoRetry/Multiprocess. Evidence destination Saved/Logs/task037/formation/{base.txt,build-*,baseline-*}; existing execution session40693.
- Router FormationJitter: level3/confidence0.49, primary main model. Read-only triage locates original failing comparison at FormationJitterSmokeCommand.cpp689: other M16 follower's hips/shoulders wobble in D-1 variant versus all-M16, unchanged factor1.25 plus5deg/s. ResetSquad, per-frame sampler and current formation-look path read; no threshold/expectation/source edits. Fresh result required before verdict.
- Coordination question624 reported prolonged live compile queue. Claude625: four worktrees building, mutex is not fair; keep waiting, kill nothing, update report and read FormationJitter code read-only. If still not built at11:30 Jerusalem, post ONCE and Claude will hold his next agent build until ours runs. No designer priority override allowed/requested.
- Next: collect existing Formation build/smoke outcome, diagnose/report; LevelGenRampSmoke follows. Do not start a replacement build while this chain is alive.


## FormationJitterSmoke — needs Claude, unchanged comparative wobble failure

- Required merged-base build finally completed before11:30 Jerusalem, PASS2.09s engine work (up to date; long wrapper wall was shared mutex waiting). Base33bfe619 merges main66318a6b docs only. No11:30 follow-up was needed; all other processes/designer priority left intact.
- Single mandatory fresh baseline FAIL88.2s, Jobs1/NoRetry/Multiprocess/fixed60fps, actual engine RESULT: FAIL. Same numeric failure as both035 runs: other M16 follower Medic-Sapper hips27.2 / shoulders24.0deg/s beside D-1; all-M16 baseline17.0 /12.6. Limits remain26.25 /20.75 (factor1.25 +5), so both channels actually exceed acceptance.
- Per-frame CSV A..E and full summaries preserved. Medic's capsule wobble23.9 versus12.2deg/s; speed199+-49 versus198+-49cm/s; slot jumps0.43/s in both; look jumps0.90 versus0.77/s. This is not merely a different clip on the D-1 actor: the other M16 actor's capsule/body signal changes. The D-1 added-wobble/clip-switch tests, leader comparison, filtered-versus-unfiltered bodies and formation-label comparison all PASS.
- Read exact smoke sampler, ResetSquad/three-second settle and comparison; runtime SquadSubsystemFormation.cpp formation-look filter/bypass path; relevant bounded history: original smoke c81585ae, latest formation edits0e9c49e7 (F-2), f65f3325 (stealth-only look handling),0b038227 (wounded follower pace). No isolated causal regression commit is proven, and no stale assertion or demonstrably safe <40-line single-system fix was established. Verdict(c): unresolved runtime coupling needs Claude, rather than widening the designed whole-group criterion or claiming an identified root cause.
- No local source/threshold/timing/config/asset/flag changes and no local-fix gate claimed. Router3/confidence0.49 | primary main session | diagnosis complete, needs Claude.
- Evidence Saved/Logs/task037/formation/: base.txt, build-wrapper.log, build-engine.log, baseline-wrapper.log, baseline-engine.log, baseline-result.json, FormationJitter-A/B/C/D/E.csv.
- Next/final original smoke: LevelGenRampSmoke. Same compiled base is usable (report-only changes); run once with required standalone flags, then read FAIL and relevant code. PMC pair remains skipped/Gemini077; corpse cost Gemini078.
