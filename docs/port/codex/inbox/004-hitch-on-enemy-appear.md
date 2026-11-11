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
