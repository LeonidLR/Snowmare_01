# 025 - Black screen ~20 s on the FIRST map start: measure where the time goes (Claude, 2026-11-16) - MEDIUM, diagnosis only
Why (designer, board 073): on the first Play of a map the screen stays black for ~20 s - only the HUD is visible and nothing works.
Stop + Play again -> normal. Suspect: the 004 world preload (UGameplayPreloadSubsystem, Codex.Preload.WorldPause pauses the engine while
preparing, ~10-18 s, 624 paths, see the top of docs/port/HANDOFF.md) on a cold cache, plus shader compilation.
Branch codex/025-first-start-diag (from main), worktree CodexTactics-agents. NO gameplay edits - report only.
Do:
1. Find where the preload logs its start / progress / release (grep "Preload" in Source/CodexTactics, ~30 lines per hit). If it does not log
   the elapsed time per phase, add a TEMPORARY log (removed before the commit) or use the existing smoke HitchProbeSmoke output.
2. Measure a COLD start and a WARM start of the same map in -game mode (map /Game/Maps/Generated/L_Gen_Depot_1 or the one in
   Scripts/test_map.json for HitchProbeSmoke): cold = first launch after a build, warm = second launch. Per run paste: time to
   "preload started", "released", first rendered world frame, shader compile messages count (grep "Compiling" / "ShaderCompile" in the log).
3. Also try -ExecCmds="Codex.Preload.WorldPause 0" once: how long is the black screen then, and does anything break (look for errors)?
4. Report: a table cold / warm / pause-off with the numbers, which step eats the 20 s, and the smallest fix you would propose (loading
   screen while preparing, shorter preload list, async shaders, ...) - do not apply.
Report: docs/port/codex/outbox/025-first-start-black-screen-diag.report.md. Close every game/editor process you start. 10 % limit rule.
