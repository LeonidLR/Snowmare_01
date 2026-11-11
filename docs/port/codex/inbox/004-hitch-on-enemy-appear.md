# 004 - Multi-second freeze when operatives move and enemies appear (designer bug report 2026-11-11)

On interim/2026-11-12, own task branch `codex/hitch-enemy-appear`. Rules: README.md (incl. "Work rules added 2026-11-11").
Do this AFTER the stealth leftovers (a), before props C5 - a regression in normal play outranks new features.

## The report
"Since about yesterday or the day before (2026-11-09 / 11-10): when the characters move across the map and enemies appear
somewhere, the game freezes for a few seconds." The designer adds the map / PIE-or-standalone / frequency in the chat - ask him
there (Russian, one message) if it is not in this file yet.

## What Claude checked (cheap, 2026-11-11)
- The current Saved/Logs/CodexTactics.log is an editor session without play - no evidence yet. The backup logs from 11-10 / 11-11
  play sessions (Saved/Logs/CodexTactics-backup-*.log) are the first place to look: `FlushAsyncLoading`, `LogStreaming`, hitch /
  long-frame lines, synchronous loads on the game thread, shader / PSO compiles at the freeze time.
- Nothing was fixed; no hypothesis is confirmed.

## Evidence from the designer's play session (2026-11-11 13:04, PIE, L_Gen_Depot_1) - log copy: MAIN Saved/Orchestrator/hitch-2026-11-11-play.log
Designer: Play took ~20 s to start; when the first enemies appeared while he walked the commander, the game froze ~15 s.
- **Start (PIE total start time 20.99 s):** frame 352 has a silent 13.9 s gap right after `[EnvTrace] start cover on`
  (13:04:22.058) until `Critical wound: ...` (13:04:35.927) - something synchronous runs under the start cover with no log line.
  Prime suspect: the operative / anim-layer setup resolving all clips synchronously (injured tiers "use ALL pack clips" + legs_abp +
  critical wound merged 11-10 = "since yesterday"). Instrument with SCOPE_LOG_TIME / Insights to name it.
- **First enemies (frame 614, ~9 s in one frame, 13:05:05 -> 13:05:14):** at 30 s the first [Pressure] event (zombie patrol) and the
  [PmcEncounter] squad spawn land in the SAME frame:
  `FlushAsyncLoading(608): 3 QueuedPackages` (2.3 s) -> ladder clip setup -> zombie spawn -> `Waiting for skinned assets to be ready
  0/1 (SKM_PmcOperator)` (3.9 s) -> 3 PMC operators spawned. So: synchronous loads on the spawn path + the PMC body mesh not
  ready (editor builds it on first use) + two spawn systems firing in one frame.
- Fix directions (verify): preload / async-load everything the spawners and anim layers need during level start (behind the start
  cover, not in a gameplay frame); remove LoadObject / LoadSynchronous / FlushAsyncLoading from spawn paths; stagger PMC encounter
  spawn vs the Pressure schedule; measure the start phase and keep PIE start reasonable. Also compare a 2nd Play in the same editor
  session (editor-only first-use compiles) - say which part remains in a standalone run (`-game`).

## Suspects (merged 2026-11-09 .. 11-11; verify, do not assume)
1. Synchronous `LoadObject` / `StaticLoadObject` of animation clips on enemy spawn / first sight (injured tiers layer, legs_abp layer,
   weapon_anim_sets.json resolution, critical wound) instead of preloading at level load.
2. JSON re-read on the game thread per spawn / per frame (hud_labels.json, formation_shapes.json live reload, critical_wound.json).
3. First-use PSO / shader compiles of enemy or overlay materials (team-colour / defense overlay MIDs, the new stealth MID).
4. Sight / perception cost spikes when many enemies enter view (TacticalSightSubsystem, PMC level-aware on-point / detour queries,
   horde spawn).
5. Navigation rebuilds triggered by spawned actors (nav modifiers, dynamic obstacles).

## Method
- Reproduce headless first: a `HitchProbeSmoke` (or extend an existing spawn smoke) on the designer's map / Depot - move the squad,
  spawn / reveal enemies, record per-frame game-thread time (FPlatformTime) and fail on any frame > 250 ms; log the top scopes.
- Use Unreal Insights (`-trace=cpu,loadtime,frame`) or `stat dumphitches` for the evidence; put the hitch frame's top scopes in the
  report. Use `test.ps1 -Smart` + the smoke for the fix.
- Find the merge that introduced it (git bisect over main 2026-11-08..11-11 with the probe, if the cause is not obvious).

## Done =
Root cause with evidence (log / trace lines, the commit that introduced it), a fix on the task branch, the probe smoke green
(no frame > 250 ms) plus the regression smokes of the touched systems, report docs/port/codex/outbox/004-hitch-on-enemy-appear.report.md.

## Designer follow-up (2026-11-11 ~13:20)
A 2nd Play in the same editor session started almost instantly -> the start cost is first-load (disk load + editor first-use builds
of the 243 clips / meshes), the assets then stay in memory. Still fix it (a packaged game pays the first load on every launch, and a
gameplay-frame freeze is never OK), but rank it: (1) the first-enemy frame (move loads out of the gameplay frame, stagger
Pressure vs PmcEncounter, prepare SKM_PmcOperator at start), (2) the start: one async preload of the anim sets per level (shared,
not per operative) behind the start cover. Measure both on a FIRST Play after an editor restart and in a `-game` run.
2nd Play: the first enemies also appeared without a freeze -> both hitches are first-load costs (sync loads + editor first-use
builds), not per-spawn CPU. The fix is therefore loading: async preload at level start behind the start cover, nothing loaded
synchronously in a gameplay frame. No sight / perception / nav suspects needed unless the first-Play probe says otherwise.
