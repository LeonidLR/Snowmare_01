# G019 - X6 sector FIGHT LOOP: rounds + ammo resupply (DESIGNER PRIORITY - ready by the morning of 2026-11-25)

Designer 2026-11-24 evening: from tomorrow he plays many 3 v 3 fights in the X6 training sectors (recorded sessions for the
trainer). Base: local main (the sectors merge 89a20cd4 / Codex 106 runtime: Saved/Training/active_sector.json, -Sector=, Codex.Sector,
Content/Data/LevelJson/L_Gen_Depot_1_X6.sectors.json). Branch grok/G019-sector-rounds from the LOCAL `main` branch (it is not on
origin yet - your worktree shares the repo, `git switch -c grok/G019-sector-rounds main`).

1. ROUND LOOP (only while a sector is active):
   - a round ends when one side is wiped (all dead / incapacitated) or after round_time_limit_s (data, default 600);
   - the player squad's death does NOT fail the mission and does not start the death cinematic in sector mode;
   - after round_restart_delay_s (data, default 8) both squads are removed and respawn: the player squad at another start, the PMC
     squad at another post of the same sector (seeded pick, never the same start+post pair twice in a row), full health, ammo reset;
   - a short HUD line "Раунд N: победа / поражение / ничья" for 3 s; round counter + score in the session recording
     (SessionRecordingRules: round_start / round_end events with result, sector id, seed, durations, kills both sides).
2. AMMO (both sides, sector mode): data field ammo_mode = "real" | "resupply" (default "resupply").
   resupply = reuse the PmcArena Ammo=resupply logic (Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp ResupplyAmmo) but in
   runtime code: a fighter whose current weapon's reserve is empty gets the weapon's full reserve back once he has been out of fire
   for resupply_delay_s (data, default 20). Grenades: back to the loadout count at round start only. Finite health, damage on.
3. Cold stays OFF (cold_rate_mult 0) - do not touch. Weather untouched.
Data: per-sector or top-level fields in L_Gen_Depot_1_X6.sectors.json (round_restart_delay_s, round_time_limit_s, ammo_mode,
resupply_delay_s) - minimal line edits, keep the file's format.
Tests: pure rules (round end, start/post pick, resupply timing) as unit tests; a smoke SectorRoundsSmoke on X6 sector 5 (force a wipe ->
next round respawns at a different pair; empty reserve -> refilled after the delay, not before). Worker slot2 only.
Report docs/port/grok/outbox/G019-sector-rounds.report.md + "G019 FINISH | ..." in chat. Never push, never touch main / maps / .uasset.
