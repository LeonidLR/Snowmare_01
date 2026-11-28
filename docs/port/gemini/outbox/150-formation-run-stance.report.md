# 150: Formation Run Order Stands Crouched / Prone Followers

- **Branch / Worktree**: `gemini/150-formation-run-stance` (`C:\Work\Lanes\CodexTactics-150`)
- **Base**: `origin/main` (`d3f6dfae3`)

## Summary of Changes
- **Pure Rule (`Source/CodexTactics/Public/Characters/SquadOrderRules.h`, `Source/CodexTactics/Private/Characters/SquadOrderRules.cpp`)**:
  - Added `FFormationFollowOrderFacts`, `FFormationFollowStanceGait`, and `ResolveFormationFollowStanceGait(const FFormationFollowOrderFacts&)`.
  - When in active formation (`!bIsSoloMode && !bFollowersHolding`) and the leader sprints (`bLeaderSprinting`), eligible followers in `Crouching` or `Prone` switch to `Standing` (`MarkStanceFromSquad()`, `bNextStoodUpForLeaderSprint = true`) and sprint (`bSprint = true`).
  - Protects followers who cannot sprint once standing, hold a covered approach look, wall pose, cover/pending cover, stealth mode, or locked prone (critical wound/frostbite).
  - When the leader returns to walk / stop / crouch (`!bLeaderSprinting`), sprint drops (`bSprint = false`); followers stood up by the sprint whose stance remains squad-owned (`IsStanceFromSquad()`) match `LeaderStance` (e.g., stay `Standing` on a single-click walk, or return to `Crouching` on the `C` key) — mirroring `SquadSubsystemRegroup.cpp`'s leader-stance copy pattern.
  - Preserves Solo mode untouched (`bInFormation = false` -> no stance or sprint change).
- **Shared Eligibility & Wiring (`OperativeCharacter.h`, `OperativeCharacterMovement.cpp`, `SquadSubsystem.h`, `SquadSubsystem.cpp`)**:
  - Extracted `AOperativeCharacter::CanSprintIfStanding()` so `OrderMoveTo`, `SetSprinting`, `CanSprint`, and `USquadSubsystem::UpdateFollower` share one sprint eligibility rule.
  - Added `bool bStoodUpForLeaderSprint = false` on `USquadSubsystem::FFollowerState` and wired `ResolveFormationFollowStanceGait` in `USquadSubsystem::UpdateFollower`.

## Verification
- **Build**: `machine_dispatch.py build --lane C:\Work\Lanes\CodexTactics-150`: `Result: Succeeded` (worker compile, Exit code 0).
- **CheckMap**: `Scripts/test.ps1 -CheckMap`: `Impact map OK: every smoke command is registered and mapped.`
- **Unit Tests** (`Scripts/test.ps1 -Filter "CodexTactics.Squad.Orders+CodexTactics.Movement+CodexTactics.Formation" -SkipSmokes -NoBuild`): **263 tests, 0 failed** (including `CodexTactics.Squad.Orders.FormationFollowStanceGait` & `StandToSprintWithLeader`).
- **Dispatched Smokes** (`machine_dispatch.py smoke --lane C:\Work\Lanes\CodexTactics-150 --smokes FormationRunStanceSmoke,FormationScanPitchSmoke` on `40768488`):
  - `FormationRunStanceSmoke`: **PASS** (`72.5s` on `L_Gen_Depot_1_X6`):
    - `Alt+C` crouched all members -> double-click sprint 20 m ahead -> both followers `STANDING` & `run` within `1 s` (`Engineer stand@0.02s, run@0.40s, speed 427 >= 408`; `Medic-Sapper stand@0.02s, run@0.20s, speed 681 >= 408`);
    - Single-click walk order -> both followers walk (`Standing`, `sprinting 0`, speed `169` / `138 < 408`);
    - `C` key (`ApplyStance(EOperativeStance::Crouching)`) on leader -> both followers returned to `CROUCHED` (`stance 1`);
    - Prone follower on formation sprint -> stood up & running (`stance 0, sprint 1, speed 420 >= 408, 2.20s`);
    - Solo mode -> leader stood up and sprinted (`725 >= 408`) while crouched followers stayed `CROUCHED` (`sprint 0`).
  - `FormationScanPitchSmoke`: **PASS** (`73.4s`).
- **Pro Self-Review**: `research` subagent (`Model: pro`) passes `17460ca3`, `4fd23665`, and final delta `6c92e610` (`CLEAN`, `0` `Content/` files); resolved all findings (`HasGroupCoverProne` cover hold guard, `CVarSquadFollowerSprintStandUp == 0` guard, `CanSprintIfStanding` frostbite guard, `bTraversalOrSniperHold`, `StoodUpForSprint` persistence across `RebuildFollowers` and pruning on role/guard/Solo/load/unregister transitions, real `C`-key `ApplyStance`, and `Followers.IsEmpty()` smoke bounds guard).
