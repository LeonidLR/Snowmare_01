# 150: Formation Run Stance & Gait Sync — Plan

- **Branch / Worktree**: `gemini/150-formation-run-stance` (`C:\Work\Lanes\CodexTactics-150`)
- **Base**: `origin/main` (`d3f6dfae3`)

## Approach
1. **Pure Rule (`SquadOrderRules`)**:
   - Add `FFormationFollowOrderFacts`, `FFormationFollowStanceGait`, and `ResolveFormationFollowStanceGait(const FFormationFollowOrderFacts&)` in `Source/CodexTactics/Public/Characters/SquadOrderRules.h` / `Private/Characters/SquadOrderRules.cpp`.
   - When `bInFormation && bLeaderSprinting` and follower is not constrained (can sprint once standing, not holding cover/wall pose/approach look/stealth/locked prone), switch crouched or prone followers to `Standing` (`MarkStanceFromSquad()`, `bNextStoodUpForLeaderSprint = true`) and `bSprint = true`.
   - When `!bLeaderSprinting`, stop sprinting (`bSprint = false`) so followers walk; if `bStoodUpForLeaderSprint && bStanceFromSquad` and `LeaderStance != FollowerStance` (e.g., leader pressed `C` to crouch), copy `LeaderStance` (same pattern as `SquadSubsystemRegroup.cpp`!) and disarm `bNextStoodUpForLeaderSprint` once lowered from Standing.
   - When `!bInFormation` (Solo, etc.), return no changes.
2. **Operative & Squad Wiring**:
   - Add `AOperativeCharacter::CanSprintIfStanding()` (`OperativeCharacter.h` / `OperativeCharacterMovement.cpp`) shared by `OrderMoveTo`, `SetSprinting`, `CanSprint`, and `USquadSubsystem::UpdateFollower` so prone followers are checked for sprint eligibility once standing.
   - Store `bool bStoodUpForLeaderSprint = false;` on `USquadSubsystem::FFollowerState` and wire `ResolveFormationFollowStanceGait` into `USquadSubsystem::UpdateFollower`.
3. **Verification**:
   - Add unit tests in `SquadOrderRulesTest.cpp` (`FormationFollowStanceGait`) covering crouched/prone sprint stand-up, single-click walk, `C`-key crouch return, manual player stance override, Solo no-op, and wounded/carrying/cover/stealth guards.
   - Add `FormationRunStanceSmoke` (`L_Gen_Depot_1_X6`), register in `Scripts/test_map.json`, verify `CheckMap`, unit tests, build, and `machine_dispatch.py smoke`.
