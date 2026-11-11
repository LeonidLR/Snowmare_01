# 014 - Read-only: find the synchronous loads behind the start / first-enemy freezes (Claude, 2026-11-11)

Context: docs/port/codex/inbox/004-hitch-on-enemy-appear.md ("Evidence" section) + the log MAIN Saved/Orchestrator/hitch-2026-11-11-play.log.
Codex fixes it in C++ after the stealth leftovers; you prepare the map so his fix is fast. Read-only, report only, own branch
`gemini/014-hitch-scan` from main. No C++ edits, no main / push.

1. Every synchronous load reachable at level start and on enemy / PMC spawn: grep Source/ for LoadObject, StaticLoadObject,
   LoadSynchronous, TryLoad, LoadClass, FlushAsyncLoading, ConstructorHelpers outside constructors, FSoftObjectPath::TryLoad,
   UAssetManager::...Sync. For each hit: file:line, the asset / path it loads (or the JSON key it comes from), the caller chain
   (level start / operative spawn / anim layer resolve / Pressure spawn / PmcEncounter spawn / per frame), how often it runs.
2. The 13.9 s silent gap at start (after `[EnvTrace] start cover on`, before `Critical wound:`): list what BeginPlay / OnWorldBeginPlay
   runs in that window (order of subsystems and actors) and which of the hits from 1 sit there. Count the clips the anim layers
   resolve (weapon_anim_sets.json injured / legs_abp / stealth blocks, critical_wound.json) and whether each is loaded sync.
3. The first-enemy frame: why the Pressure first event (30 s) and the PmcEncounter spawn land in the same frame; which assets
   they load (zombie BP, ladder clips, SKM_PmcOperator + materials).
4. A ranked fix list for Codex: preload point (where in level start), which loads to make async / move, expected saving per item.
Output: docs/port/gemini/outbox/014-hitch-scan.report.md. One chat line with the hash when done.
