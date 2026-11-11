# 002 - Finish wounded-follower formation pace

Status: complete and verified; ready for Claude review. No push or merge.
Branch: `fix/wounded-follower-pace` (the task explicitly continues this existing branch).
Worktree: `C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics-woundfollow`.
Starting WIP: `5dc9788b`, parent/base `959f663e`. Task 001 was reviewed and merged by Claude before this work started (agent-chat message 109).

## Implementation reviewed and completed

Read `git diff main...HEAD` and the inherited implementation. The WIP already implements the requested behavior, so no further runtime changes were needed after review and verification. It is consolidated into one proper Conventional commit on this branch: `fix(squad): keep wounded followers at the formation pace`.

- `SquadWoundPaceRules.{h,cpp}`: select the slowest wounded, non-critical member in formation; leader pace is min(normal patrol pace, `pace_share` x that member's allowed walk). Cap the wounded follower's catch-up and fixed-speed move at his allowed walk; end the leader's sprint while the pace applies.
- `SquadSubsystemWoundPace.cpp`, `SquadSubsystem.{h,cpp}`, `SquadSubsystemFormation.cpp`: collect member facts, reset frame state, apply pace before mode selection, and apply follower caps. `OperativeCharacter.{h,Movement.cpp}` adds `GetAllowedWalkSpeed` (stance, wound tier, carrying, cold; no sprint).
- **Completion fix:** `Content/Data/AI/squad_wound_pace.json` existed untracked beside the WIP. It is now included in the commit, so a clean checkout has the designer-editable toggle. Defaults: enabled 1, pace_share 0.8, limit_sprint 1, cap_wounded_follower 1; console `Codex.Squad.WoundPace -1` reads the file, 0 disables, 1 enables.
- Existing `SquadWoundPaceRulesTest.cpp` has seven focused rule tests. `WoundedFollowerSmokeCommand.cpp` measures pace, speed, slot error, body-facing vs formation look, directional injured clips, wounded leader, sprint and critical wound. `Scripts/test_map.json` registers the smoke and verification impact.
- Updated CHANGELOG, HANDOFF, TANDEM and `docs/systems/squad-and-orders.md` with verification results.

**Critical exclusion:** `UpdateFormationWoundPace` marks a member critical from `GetWoundTier() == Critical`; `SquadPace` skips him. He stays prone and crawls after his slot at his own capped crawl speed, without slowing the rest of the squad. Solo/own-order/fast-regroup/panicking/knocked-down/mounted members do not set formation pace either. The game does not promise to keep a critical crawler within the walking formation: heal him to rejoin its pace.

**Fast regroup:** its existing tier rules remain intact: light wound uses the permitted wounded run (480 x 0.65 = 312 cm/s), heavy wound uses the heavy walk, wounded members never sprint. A regrouper sets no formation pace until he joins; a heavy regrouper may need the moving leader to stop to close the gap. The pure regression test and FastRegroupSmoke pass.

## Checks and measurements

- `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`: **Succeeded**, exit 0, 4.20 s (existing WIP binaries up to date).
- `Scripts/test.ps1 -Filter CodexTactics.Squad`: **39 tests, 0 failed**, including all seven `Squad.WoundPace.*` cases.
- `Scripts/test.ps1 -Filter CodexTactics.Animation.Injured`: **9 tests, 0 failed**.
- `Scripts/smoke_parallel.ps1 -Commands CodexTactics.WoundedFollowerSmoke,CodexTactics.InjuredLocomotionSmoke,CodexTactics.FastRegroupSmoke,CodexTactics.FormationLabSmoke,CodexTactics.SoloStartSmoke -Jobs 2 -ResultJson Saved/Logs/SmokeParallel-task002.json`: **5/5 PASS**, no retries. Durations respectively 93.7 / 94.3 / 55.7 / 198.4 / 50.5 s; wall time 294.1 s.
- Extra rendered `CodexTactics.WoundedFollowerSmoke shots`: **PASS**, offscreen game camera, fixed 60 Hz step, fixed seed, formation start; no editor UI and no asset saves. Both requested light/heavy frames were visually inspected.
- `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`: **PASS** before final commit, auditing the full staged implementation after consolidating the WIP. This older branch predates the Codex boundary entry; the current main-checkout audit script was additionally run from this worktree's working directory to enforce the actual Codex protected paths: **PASS**.
- `git diff --check`: **PASS**.

Headless and rendered runs give the same key results:

| Scenario | Measured behavior |
|---|---|
| Rule off, light follower | Leader 176 cm/s (old patrol pace). |
| Light follower, rule on | Leader 114 cm/s; wounded max 143 cm/s (allowed 143), zero overspeed samples; slot p95 34 cm; faces sector 100%, no rear-turn; injured backward/diagonal clips weight 1.00. |
| Heavy follower, rule on | Leader 79 cm/s; wounded max 99 cm/s (allowed 99), zero overspeed samples; slot p95 53 cm; faces sector 100%, no rear-turn; injured directional clips weight 1.00. |
| Light-wounded leader | Leader 114 cm/s; fit followers slot p95 19 / 22 cm, at most -4 / -3 cm ahead of their slots (no overrun). |
| Sprint order, light follower | Sprint ends after 0.02 s; leader 114 cm/s. |
| Critical follower | No wound pace; leader 176 cm/s; member prone, max 40 cm/s (allowed 40). |

## Evidence and boundaries

Worktree evidence: `Saved/Logs/Build.log`, `Saved/Logs/002-CodexTactics-Squad.log`, `Saved/Logs/002-CodexTactics-Animation-Injured.log`, `Saved/Logs/SmokeParallel-task002.json`, `Saved/Logs/Smoke-WoundedFollowerSmoke.log`, `Saved/Logs/Smoke-WoundedFollowerSmoke-rendered.log`, and the other four smoke logs.

Exactly two game-camera frames copied to the task-authorized main folder and verified by hash:
- `Saved/Orchestrator/woundfollow/game_on_light.png` (40% HP follower).
- `Saved/Orchestrator/woundfollow/game_on_heavy.png` (25% HP follower).

Only these frames, this report and the explicitly requested agent-chat command write to main. No maps, configs, VFX, .uasset files, designer Depot JSON or CLAUDE.md modified. No merge, push or editor asset saves. Task 001's commits are not cherry-picked or merged here; Claude owns integration into current main.
Open questions: none. Awaiting Claude review.
