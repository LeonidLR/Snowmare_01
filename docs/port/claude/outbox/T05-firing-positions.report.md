# T-05 - firing positions that can actually fire (report, WIP)

Agent: Claude sub-agent, worktree `CodexTactics-agents`, branch `claude/firing-positions` from origin/main c72394c0. Not merged.

## Handover (kept current)

**Done (phase a, code complete, smoke not yet green):**
- `AI/FiringPositionRules.{h,cpp}` pure rules (F2, crossfire, covered share, pickers, config), JSON block `firing_positions` in `Content/Data/AI/pmc_commander.json`.
- `AI/FiringPositionSubsystem.{h,cpp}`: candidates (cover slots via `UPmcSquadSubsystem::CoverSlotAlongRay`, tactical roof edges with rim step, windows), real-shot-line validation (ECC_Visibility, operatives/turrets ignored), per-area cache, `PositionsCovering`, `PickCrossfirePair`, `DiagnoseRoofSpot`, console `CodexTactics.FiringPositions`.
- Unit tests `CodexTactics.AI.FiringPosition.*`: 5 tests, 0 failed (run locally with `Scripts/test.ps1 -Filter CodexTactics.AI.FiringPosition` on DLLs built by the worker).
- `FiringPositionSmoke` (X4) written and registered in `Scripts/test_map.json`.
- Remote build on codex-worker OK (twice, exit 0).

**Gate state now:** `FiringPositionSmoke` FAIL at 50 s on the worker (log `Saved/Logs/Smoke-FiringPositionSmoke.log`); cause not analysed yet.
Trap: another agent's worker sync resets the worker tree/DLLs between my build and my smoke; run `Scripts/Tools/worker_sync.ps1 -ShipHead -Commands <smoke> -Jobs 1 -NoRetry -AllLogs` (ships HEAD + DLLs and runs in one locked session) instead of `smoke_parallel -Remote on`.

**Next:** read the smoke log, fix, get the roof diagnosis numbers, gate phase a, then phase b (`AssignFiringPositions` in `PmcSquadSubsystem::RefreshDefence`, stance at the post, behind `firing_positions.defence_use`).
