# 069 - Mantling: READ-ONLY code scout for the C++ implementation (Claude, 2026-11-15)
Суть: подготовить точную карту кода, чтобы завтрашний агент Claude написал перелезание быстро и без поисков.
Реестр: mantling
Base: origin/main; NO code changes. Only file you write: docs/port/gemini/outbox/069-mantling-scout.report.md (commit it on a branch
gemini/069-mantling-scout in CodexTactics-interim).

## Read first
docs/design/MANTLING.md, sections 11.1-11.6 (11.6 = the designer's answers: trigger C, standing fallback, crouch on top, solo first,
Depot obstacle rework table). Clips: /Game/Characters/Animations/Mantle/{Male,Female} on branch origin/wip/2026-11-15-pause
(Scripts/Editor/setup_mantle_animation.py; rise ratio male x1.084, female x1.039 vs source).
## Answer, each with file:line
1. Movement: where the operative's CMC / movement mode lives; how ladders enter / leave their mode (LadderClimb* code) - the closest
   existing pattern for MOVE_Custom mantle; what in ladders must be copied (input lock, AI order pause, nav link, smokes).
2. Root motion / montage playback for operatives today: is Motion Warping enabled (plugin in .uproject / Build.cs)? where montages are
   played on operatives, and how weapon layer / upper body is disabled during a full-body montage (ladder, knockdown, healing).
3. Trigger C (from 11.6): where the move order / path following runs, where an obstacle on the path could be detected (trace site),
   how solo vs formation moves differ there.
4. Nav: how ladders feed the navmesh (nav links / custom areas); what a mantle link would need; does the generated Depot have nav
   on obstacle tops.
5. Obstacle data: where the J height standard landed (levelgen JSON / props validator) and how the C++ can read the obstacle class.
6. Risks: which systems will fight a mantle (cover system snapping, formation follow, stealth, injured locomotion, AI under fire) -
   with the exact functions to guard.
7. The rise-ratio question: warp window absorbs ~8 % vs scale root translation - recommend one with the reason.
8. A proposed file list + test list (unit names CodexTactics.Characters.Mantle.*, smoke MantleSmoke on which map).
## Rules
Read-only for Source/ and Content/. No guesses: if you did not find it, write "not found" and where you looked.
## Report
docs/port/gemini/outbox/069-mantling-scout.report.md (Russian prose, English identifiers), sections 1-8 as above.
