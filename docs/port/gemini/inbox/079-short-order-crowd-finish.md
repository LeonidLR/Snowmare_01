# 079 - Short move orders (< 120 cm) end without a step when a team-mate pushes (crowd "moved too far") (Claude, 2026-11-17) - MEDIUM, C++
Base: main. Branch gemini/079-short-order. Worktree CodexTactics-gemday (one branch at a time).
Why: your own 075 report (docs/port/gemini/outbox/075-cover-wall-end-standoff.report.md, "Решения для дизайнера" point 3): the crowd
following's "moved too far" check completes short orders (< 120 cm) with no step when the operative is pushed by a team-mate. This is the
remaining CoverSmoke flake "unknown threat, moving to his right ... cvr_std_idle_L" (fails on main too, ~1 in 4-8 runs).
Do:
1. PLAN: reproduce with CoverSmoke (8 runs, quote the FAIL lines + the crowd / path-following log lines), find where the order is ended
   (path following / crowd / OperativeCharacterMovement - quote file:line).
2. Fix: a short order ends only when he REACHED its goal (or the goal became unreachable), not because the crowd displaced him; numbers in
   the movement JSON config. Keep F-2 (merged today: the leader takes the corner himself, FormationLeaderCornerRules) as it is.
Tests: CoverSmoke x8 (all PASS), CornerHoldSmoke, CornerEntrySmoke, GroupCoverSmoke, SquadControlSmoke, the Formation* smokes once, unit
tests CodexTactics.Formation + CodexTactics.Tactics.
Allowed paths: Source/CodexTactics/Private/Characters/OperativeCharacterMovement.cpp, OperativeAIController.*, the crowd / path following
helpers they use (name them in the plan first), movement JSON config, tests, plan + report. Formation / cover rules: read only - ask first.
