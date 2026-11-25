# G020 - take over Codex 117: LevelGenBuildingsWalk 0/6 on the rebuilt Showcase Kit (Codex at its weekly limit until 11-30)

Base: branch codex/117-walk-ramp-pmc at c1b9d32d (Codex WIP: SmokeUtils::StopLevelPmc in the Showcase movement smokes, build PASS).
In your worktree: `git switch -c grok/G020-walk-doors codex/117-walk-ramp-pmc` (shared repo, the branch is local).
Read first: docs/port/codex/inbox/117-walk-ramp-pmc.md, chat #1137-1138.

Codex result on worker slot1 (canonical map BB9787F9, /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -ForceMap):
Ramp 36/36 PASS; Walk 0/6 buildings - entries 96/100 but no complete path; Walk log line ~1851: 212 doors shut, 30 locked
(log Saved/Logs/Worker/20261125-073442 in C:/Work/Lanes/CodexTactics-cx117). Suspect: your G018 doors now register from the v2 1-464
layout and closed / locked inner doors cut the walk nav (the walk squad does not open doors, or locked = steel story doors block rooms).

Do:
1. Controlled comparison on the same map: Walk with doors as now vs all doors forced open (smoke option or cvar, smoke-only) -> name the cause.
2. Fix at the cause, smallest change: either the smoke opens / ignores non-story doors before walking, or the walker uses the G018 open
   path, or the generator must not mark inner flat doors locked (designer rule: flat doors closed, story-locked = steel, NOT every door).
   Do not change gameplay door rules without saying so in the report.
3. Prove on slot1: Walk 6/6 + Ramp 36/36 on -Map /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit -ForceMap (in Git Bash: export MSYS_NO_PATHCONV=1).
Report docs/port/grok/outbox/G020-walk-doors.report.md + "G020 FINISH | ..." in chat. Never push, never touch main / maps / .uasset.
