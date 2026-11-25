# Gemini 127 (router first; Carbon for the final fix check) - door breach ENTRY regressed: the squad clumps instead of clearing corners (designer 2026-11-25) - right after 125

Designer: in formation the squad stacks at the door correctly. Then "enter" (click inside): it USED to work - #1 enters and clears the
RIGHT corner, #2 clears the LEFT corner, #3 enters the middle. NOW they clump into a pile and walk in together. "The mechanic was
interesting - fix it."
Code: Source/CodexTactics/Private/Core/CodexTacticsPlayerControllerBreach.cpp, Tactics/DoorwayBreachEntryRules.cpp,
DoorwayBreachRules.cpp, DoorwayBreachGhostActor.cpp; smokes CornerEntrySmoke, DoorBreachSmoke, FlankBreachSmoke (Source/CodexTacticsDev).
Likely regressors (recent merges touching doors / paths / formation): Codex 117 (b2b5e513: door auto-open along the crowd segment, closed
doors no longer DoorClosed nav area), Grok G018 doors (907e3ac9), Grok 120 (ade54f85: AOperativeAIController::FindPathForMoveRequest
filter, low-cover hold), Gemini 122. Do:
1. Repro: CornerEntrySmoke / DoorBreachSmoke on the worker at origin/main - do they still check the corner roles (right / left / middle
   end positions)? If they PASS while the game clumps, the smoke misses it: add the role check (each of the 3 ends within 1 m of its
   corner / middle point, distinct, in order) on a kept map (Showcase Kit or X6 building door).
2. Bisect with that check between 386add9b7-era main (a known good day - pick a commit from 11-23 where CornerEntry passed) and
   origin/main; name the first bad commit and the exact cause.
3. Minimal fix at the cause (keep the other commit's intent); unit test for the pure rule if any.
BUILD ONLY ON THE WORKER; full smoke set only at the end (CornerEntry, DoorBreach, FlankBreach, Doors, InteriorExit).
Checklist #1104. Report docs/port/gemini/outbox/127-breach-entry.report.md + "127 FINISH | ...". Never push.
