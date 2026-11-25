# Gemini 122 (Hydrogen) - X6 sector 2 start on a box + panic 5x too frequent (designer's 2nd sector fight 2026-11-25) - BEFORE 119

Branch gemini/122-sector2-panic from origin/main, worktree C:/Work/Lanes/CodexTactics-122. BUILD ONLY ON THE WORKER (Scripts/build.ps1 -Remote worker).
1. SECTOR 2 START: the squad spawns standing ON TOP of a block (screenshot: Engineer / Commander / Medic-Sapper on a box roof) - "too
   exposed". Fix in Content/Data/LevelJson/L_Gen_Depot_1_X6.sectors.json (minimal line edit, keep format): every start of sector 2 must be
   on the ground, on navmesh, not on a prop / roof, ideally next to cover. Check ALL starts and posts of sectors 1-6 the same way (a small
   check script or smoke: spawn point z vs ground trace, on navmesh) and fix the bad ones; list them in the report.
2. PANIC ~5x LESS (designer: "good mood, but too often; it seemed to get stuck - after the first time it came more and more often").
   Code: Source/CodexTactics/Private/Characters/PanicComponent.cpp (AttemptTriggerPanic ~162: deterministic triggers HP / cold / low
   reserve ammo / stress), PanicRules, GameBalanceConfig panic_* fields. Likely escalation cause: the low-ammo trigger (reserve <= 5) fires
   again and again in sector mode while the reserve is empty and waits for the 20 s resupply. Do:
   a) measure first: count panic triggers by reason in a 3v3 sector run (log or a smoke counter) - report before/after;
   b) add a trigger chance (data, default 0.2 = 5x fewer) rolled on the Combat random stream once per trigger EVENT (not per tick), plus a
      per-operative cooldown after a panic ends (data, default 45 s); no low-ammo trigger while the sector ammo_mode is "resupply" and a
      refill is pending; pure rule in PanicRules + unit tests (edges, determinism with a fixed seed);
   c) new fields go into the C++ config struct with defaults; do NOT edit DA_GameBalanceConfig.uasset (designer tunes it in Unreal) -
      say in the report which asset fields he can tune.
Smokes on the worker slot2 -Jobs 1: PanicSmoke (or the existing panic tests), SectorRoundsSmoke. Checklist #1104.
Report docs/port/gemini/outbox/122-sector2-panic.report.md + "122 FINISH | ..." in chat. Never push.
