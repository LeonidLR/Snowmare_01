# 095 - Can an enemy fighter walk every stair, ladder, fence and drop of a generated map? (smart enemies T-12) - MEDIUM, C++
Limit rule: at 10 % of your daily limit left: stop, commit (WIP is fine), handover at the top of the report, FINISH line.
Base: newest main. Branch gemini/095-pmc-traversal-smoke. Worktree CodexTactics-gemday. PLAN first in chat topic `gemini-095`.
Router: L3. Push may be refused by the pre-push guard (paths outside the gemini allowlist) - expected; commit locally, Claude merges.

## Design gate (lead filled)
- Spec: section **T-12** of `docs/design/gdd/smart-enemies-roadmap.md` (read it in full). Changes vs that text (lead 2026-11-21):
  owner = you (Codex is gone); worktree = CodexTactics-gemday; `L_Lab_Alley` does not exist yet -> run on `L_Gen_Depot_X4` only
  (second map comes with T-02 later).
- Acceptance: `PmcTraversalSmoke` on X4: one hostile operative (team 1, commander off) is ordered across every stair flight,
  ladder (>= 7 rungs), mantle link and drop link in turn; prints per link kind "N of M crossed" + failed ids with reason
  (no path / stuck / refused); passes at >= 95 % per kind. If today's rate is lower, register it `knownFail` in
  Scripts/test_map.json WITH the numbers in `_why` (the fixes are separate tasks - this is the one place where knownFail is allowed,
  because the brief says so).
- Reuse, do not reinvent: the probes of `LevelGenRampSmoke`, `LevelGenLaddersSmoke`, `MantleFenceRouteSmoke`, and the new walk mode
  of `LevelGenBuildingsSmoke` (its leg runner / stuck detection - on branch claude/building-generator, not in main yet: read it with
  `git show origin/claude/building-generator:Source/CodexTacticsDev/Private/Debug/LevelGenBuildingsSmokeCommand.cpp`, copy the
  pattern, do not merge that branch).
- Designer decision needed? none.

## Builds
The designer's editor may be open in the main folder. Before every build:
`Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Select CommandLine` - an UnrealEditor with CodexTactics.uproject
(no -game / -run=) running -> do NOT build; post "waiting for editor close" once, re-check every 15 min, meanwhile write the code
and the plan. Never run the second machine yourself (Claude runs worker_sync on request: post "095 needs worker build" with your commit).

## Never
Lower the 95 % line; skip a link kind that fails; game fixes in this task; touch maps / Content / Config.

## Gate
build (or Claude's worker build) | `PmcTraversalSmoke` + `LevelGenRampSmoke` + `LevelGenLaddersSmoke` |
`Scripts/Tools/gemini_gate.ps1 -Task 095 -SonnetReview` | `git diff --check`.
Allowed paths: `Source/CodexTacticsDev/Private/Debug/` (new smoke file), `Scripts/test_map.json` (one row + registry/impact lines),
`docs/port/gemini/**`.

## Report
`docs/port/gemini/outbox/095-pmc-traversal-smoke.report.md` (Russian): the table per link kind, failed ids + reasons, pasted smoke lines.
FINISH in topic `gemini-095`: `095 FINISH | <status> | <commit> | done: ... | NOT done: ... | needs Claude | gate: ... | report: <path>`.
