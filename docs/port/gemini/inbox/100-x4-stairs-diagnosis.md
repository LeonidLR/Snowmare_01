# Gemini 100 — why 6 X4 stairs fail for everyone (DIAGNOSIS ONLY, no fix)

Branch `gemini/100-x4-stairs-diagnosis` from the newest main. Output = a report with evidence. NO gameplay code change.

## Context

Task 095 (merged 071b324e, `CodexTactics.PmcTraversalSmoke`, report `docs/port/gemini/outbox/095-pmc-traversal-smoke.report.md`)
found that on `/Game/Maps/Generated/L_Gen_Depot_X4` the same 6 stairs fail in all four passes (PMC solo, PMC formation, team-0
solo, team-0 formation): 13/19. Every failure is "no march seen" - the operative never starts climbing. Fable noted: STAIR_in_8 and
B_x2_4_t8_hall_stair_0 end on the upper level 2.5-3.5 m from the goal `Top + Inward*200` (PmcTraversalSmokeCommand.cpp ~461) - maybe
a goal artefact; three `*_block_stair_0` stand on the ground under the goal - maybe a partial path (map / navmesh).

## Do

1. List the 6 failing stairs (ids, positions, storey heights) from the 095 worker logs / a fresh run.
2. For each, find the cause with evidence: navmesh path from the start to the stair top (partial? which poly ends?), the goal point
   (inside geometry? off the navmesh? on the wrong storey?), the stair geometry in the layout JSON / tactical file, and compare with
   one stair of the same kind that passes.
3. Classify each: (a) smoke goal artefact, (b) navmesh / nav link gap, (c) layout data error, (d) other - with the log lines.
4. Propose the minimal fix per class (where, what) - do NOT implement it.

Smoke runs ONLY on the worker: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/worker_sync.ps1 -RemoteRoot
C:/Work/CodexTactics-slot1 -ShareMachine -ShipHead -Jobs 2 -Commands PmcTraversalSmoke` (day 08:00-16:30; add `-SmokeExtra` for
`-TraversalOnly` / stairs-only args if the smoke has them). A debug log line in the smoke is allowed (Debug folder only).

Allowed paths: `Source/CodexTacticsDev/Private/Debug/PmcTraversalSmokeCommand.cpp` (log lines only),
`docs/port/gemini/outbox/100-*`. Never: navmesh / movement / AI / levelgen code, maps, .uasset.

Report `docs/port/gemini/outbox/100-x4-stairs-diagnosis.report.md` (Russian): table stair -> class -> evidence (log line / file:line) ->
proposed fix. FINISH line in topic gemini-100.
