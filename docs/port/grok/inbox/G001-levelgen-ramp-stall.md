# G001 - LevelGenRampSmoke: the squad stalls on the stair top (Claude, 2026-11-17) - MEDIUM, C++ (trial task)
Branch grok/G001-ramp-stall from the newest main, worktree CodexTactics-grok (build it first - ~10-15 min).
Why: the smoke `LevelGenRampSmoke` (L_Gen_Depot_1, -LevelJson=L_Gen_Depot_1.json; test_map.json has its map / args) fails every run:
"STAIR_in_8 group (all three box-selected, sprint): STUCK after 17 s (top level z 300)" - each member has a COMPLETE path that ends 0 cm
from his goal at z 310, yet speed 0 and "still" for 4-10 s; one of them is at feet 8 cm (still at the bottom). Codex 037 could not isolate
the cause: read its section in docs/port/codex/outbox/037-red-smokes-triage.report.md (on main) first. History of the involved files:
Source/CodexTacticsDev/Private/Debug/LevelGenRampSmokeCommand.cpp, Source/CodexTactics/Private/Characters/SquadSubsystemRampFile.cpp
(the ramp file = the squad goes up a ramp one at a time), the crowd / path following of the operatives.
Do:
1. PLAN: run the smoke once alone (-Jobs 1), quote the FAIL lines and the 3 members' state per second around the stall (add a temporary
   verbose log in the smoke if needed - remove or keep it behind a cvar). Find WHY a member with a complete path stands still: the ramp
   file gate (waiting for the man ahead who never "clears"), a crowd avoidance deadlock at the stair mouth, a sprint group target off the
   roof edge, the nav link / stair end. Quote file:line.
2. Fix the cause in the ramp file / squad movement (numbers in the movement / squad JSON config), never by changing the smoke's goals or
   timeouts. If the smoke's expectation is genuinely stale (a design change made it wrong), STOP and ask Claude in the chat first.
Gate: LevelGenRampSmoke 3/3 (-Jobs 1), FallDropSmoke, RoofCutawayFocusSmoke, SquadControlSmoke, CoverSmoke, CodexTactics.Squad +
CodexTactics.Formation unit tests, typesafe audit --agent grok. Report + FINISH.
Allowed paths: Source/CodexTactics/Private/Characters/SquadSubsystemRamp*.cpp, Source/CodexTactics/Public/Characters/SquadSubsystem.h
(only ramp-file members), the ramp / movement JSON config under Content/Data/Movement/ (name the file in your plan),
Source/CodexTacticsDev/Private/Debug/LevelGenRampSmokeCommand.cpp (logging only), Source/CodexTacticsTests/** (new tests),
docs/port/grok/outbox/G001-*. Anything else -> ask first.
