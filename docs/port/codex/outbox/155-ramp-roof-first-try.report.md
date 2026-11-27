# 155 - first roof order via a ramp

Status: validation in progress; **not yet MERGE-READY**. Existing lane and WIP `4b725e7d` preserved. Merged `origin/main` `058e0900` in `55c7b793`; then merged156/current main `839268f6` in `4ae39828`. Implementation/checks before that integration: `870acc85`. No protected designer data edits.

## Findings and changes

The supplied 07:05:30 excerpt is not the recorded round-four roof attempt. It contains ground cover orders (Z10) around (-2597,-11215). Those ground orders cannot establish a failed ramp link. The recording does not retain the original cursor ray/hit.

The actual round-four start is t825.9511. Commander18 first orders at t875.9157 / t876.0885 target (-9107,-11116,900) / (-9111,-11096,900), from crouched cover at (-7398,-12825,feet10). He stops at (-7763,-11531,feet251) by t886 and remains there until t899; selection changes near the stop. The later order t907.9543 / t908.1247 to (-7574,-11468,900) reaches the roof by t920. The relevant access is `B_x1_10_t7_store_stair_0`.

Code defect: legacy vault and obstacle step-off call `StopMovement`, whose completion clears `bOwnMoveOrder`. Their resumed `RequestMove` previously did not restore it. Switching selection in Solo can then stop that resumed walk as a follower. Preserve ownership across vault, step-off, mantle-hop and replacement orders during a vault. The existing mantle implementation already preserves its equivalent bit. `Codex.Debug.VaultResumeOwnOrder155 0` permits a same-binary comparison with the prior behavior; default 1 enables the fix. Runtime confirmation of the exact replay is pending below.

152 overlap reviewed after the main merge: retain `bOrderSprint`, `PauseMoveForTraversal` and `RequestMove`/tick `RefreshOrderSprint` unchanged. 152 retains requested gait; 155 separately restores ownership after the resumed request. The 135b/153 preview and cover changes merged without conflicts. The later156 integration preserved its persistent player-cover intent and transition hysteresis; conflicts in `Scripts/test_map.json` and HANDOFF retained both tasks' entries. Gameplay code merged automatically and was reviewed; mapped regressions remain required.

Case 2 is building **64, B_x1_16_t7_block**. The full play log identifies `StaticMeshActor_1019` at09:00:41 and `StaticMeshActor_1026` at09:01:17 as window-sill dispatch with destination Z10. Thus these orders were rewritten before movement. The screenshot also shows the ladder ending below the upper surface. Historical raw cursor hits are absent, so the precise historical ray cannot be proved from the logs.

Two code interception defects were found: high-hit lowering accepted another actor below on matching normal alone; the window-face pick extended260cm above its sill, while through-window landing classification checked only XY. Fixes preserve ramp/slab/parapet/window hits, validate lower-hit identity, restrict window picks to their opening/storey, and keep an elevated parapet click on the roof floor. Pending cover/window target matching also compares height. Elevated own moves now require landing navmesh near the requested height and a complete path; a refusal posts **No way up** to the HUD message feed and spawns floating **NO WAY UP - CHECK RAMP / LADDER** text.

Evidence read without changing the play copy: both supplied excerpts, case-2 screenshot, recording `2026-11-27_L_Gen_Depot_1_X6_085013.jsonl`, current full play log (case2; the earlier case1 session is no longer in that log).

## Acceptance and map audit

- `CodexTactics.LevelGenRampSmoke designer155 trace`, mapped to `/Game/Maps/Generated/L_Gen_Depot_1_X6`: stages the recorded cover and squad positions, sends the original first roof destination as the recorded double-click sprint, switches selection at+10s, and requires actual feet on the roof near the requested destination. No later retry roof order. `roofedge` remains available for real placed parapet-hit traversal cases.
- `CodexTactics.RoofAccessAuditSmoke`, same X6 map: inventories every placed ladder and collision-enabled sloped ramp slab, resolves building numbers through148's `buildings_index.json`, measures top/claimed landing heights, infantry navmesh projections, enabled ladder links and endpoint path/link IDs, ramp surface coverage and the direct join at the top. Inventory must cover at least the layout's25 ladders and24 stairs. Writes `Saved/Logs/155-roof-access-audit.md`; its complete actor table will be attached to this report after execution. A smoke PASS means audit execution/code checks passed, **not** that the map is defect-free.
- Audit smoke also checks all elevated parapet faces retain height and avoid window picks, dispatches an actual case2 parapet through `HandlePlayerClick`, and checks refused elevated movement produces both a HUD feed entry and live floating text.
- The audit does not edit map geometry, LevelJson, assets or designer files. All proposed data repairs are for Lead/designer application.

## Validation

Pending worker build, Smart unit tests, mapped smokes and same-binary ownership comparison. The Smart plan resolves21 mapped checks. `test.ps1 -CheckMap` and `git diff --check` pass. Unity scan finds seven pre-existing collisions, all in unchanged files.

The first worker build was queued17:05 behind144, the Lead build and subsequent work. At17:25 the existing secondary root `C:/Work/CodexTactics-slot1` was also busy. Lead1561 explicitly permitted reuse once idle. At17:28:46 `machine_dispatch` reported it free; only our waiting9540 was cancelled and the existing `-ReclaimStale` cleared its orphan SYNC (logged). Unmodified `worker_sync -RemoteRoot C:/Work/CodexTactics-slot1 -ShareMachine -ShipRef870acc85 -Build -FetchBinaries` acquired that root and began `Scripts/build.ps1`. The transport invoked `git lfs push origin870acc85` (objects only), bundled the commit and reset the worker root to that pinned SHA; no branch refs were pushed. Main advanced with156 during this build, so it is a pre-integration build and is not final acceptance evidence. Final results/root/SHA will be recorded here.

Earlier WIP attempts are not acceptance evidence: the initial worker compile exposed a missing `NavigationSystem.h` include (fixed); an early Smart run before lane DLLs existed matched zero tests.

## Boundaries and integration

Removed the unrelated `Scripts/Tools/worker_sync.ps1` change in `33e634f5`; final transport script equals origin/main. Use the existing permitted transport and report its actual effects; no replacement infrastructure edit. No branch refs or main are pushed by this executor.

139 is still separate at this main baseline. Its `HasNoWayUp` guard overlaps `RequestMove`; Lead must reconcile these guards when integrating139 and retain139's authored standing-capsule allowance for legacy ground goals. No139 designer correction, including LadderActor_22, is applied here.
