# Gemini 105b-sector-fight-check (Hydrogen) - step 5 of your 105, now that sectors + rounds are in main

origin/main has the X6 sectors (105 data, Codex 106 runtime: walls + nav + clamp) and Grok G019 rounds + ammo resupply
(SectorRoundsSmoke). The designer starts recorded 3 v 3 fights in the sectors today. Check every sector BEFORE he hits a bad one:
- For sectors 1-6 x seeds 1-3 (18 runs) on the worker slot2, -Jobs 1 (slot1 may be busy with Codex 117): start the sector (-Sector=N),
  let the fight loop run 2 rounds. Use SectorRoundsSmoke if it accepts sector / seed args (read its source first), otherwise add those
  args to it (minimal, in a branch).
- Per run report: first-contact time (target < 90 s), round results, any unit outside the box, any unit stuck > 10 s, resupply events,
  PMC actually engaging (not idle at the post).
- One table (sector x seed) in docs/port/gemini/outbox/105b-sector-fight-check.report.md; list the bad sectors / start-post pairs with the
  log line. No data changes unless a start or post is clearly broken (then a minimal line edit in L_Gen_Depot_1_X6.sectors.json + say why).
Standing checklist #1104 applies (kept maps only, minimal edits, same seed both arms, re-read chat, git diff --stat). Branch gemini/105b.
