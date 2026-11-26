# Gemini 148 (L3; pro-subagent self-review + one api_task Sonnet review; Lead reviews) - Training Ground: 2D fight replay + designer's drawn routes (designer 2026-11-26)

Goal: the designer re-watches his sector fights top-down, comments, and DRAWS how it should have been done; a script turns the drawings into
data the trainer / Lead can use. No video rendering, no UE: a TG tab reading Saved/Demos/*.jsonl in the browser.
A. Replay tab "Хроника боя" (Tools/TrainingGround):
   - pick a recording (list from Saved/Demos, newest first, with sector / round / result / duration);
   - map from Content/Data/LevelJson/L_Gen_Depot_1_X6.json: buildings, fences, walls, low covers, ladders / ramps, sector boxes (outline only);
   - units: our operatives + PMC as dots with facing (optional vision cones), name labels, roof / height marker, orders as faint lines;
   - events when present (121 format): shots (line shooter -> target), hits, kills / downs, stealth, cover in / out; old format: positions,
     orders, round result only - must still play;
   - timeline: play / pause, x1 / x2 / x4, scrub, event ticks on the bar; keyboard space / arrows.
B. BUILDING NUMBERS (sub-task): every building gets a short stable number on the map (per map, e.g. 1..N ordered west->east, north->south),
   stored once in Content/Data/LevelJson/L_Gen_Depot_1_X6.buildings_index.json {number -> building id}, so "здание 2" means the same house
   in every replay. If cheap, the same number can be shown in game as a roof label toggle (report the cost; do not do it if > small).
C. ANNOTATION TOOLS (sub-task, the main value):
   - text comment pinned to a time (and optionally to a map point / unit);
   - DRAW: freehand route / polyline with smoothing (spline), arrow, circle / zone, each with a colour, an optional unit (who) and a label;
     edit / delete; undo;
   - saved next to the recording: Saved/Demos/<name>.notes.json, world coordinates (cm, same frame as the recording), time, author.
D. DECODER script Scripts/Tools/demo_notes.py: reads .notes.json + the map -> plain facts, e.g.
   "t=02:14 Commander: route passes building 2 on its RIGHT (east) side, 1.5 m off the wall, then to the low cover at (x,y)",
   "zone: overwatch from the roof of building 5", per route: buildings passed with side, covers touched, length, crossings of open ground.
   Output a short .md + JSON; unit tests for the side-of-building / passing rules.
- Node tests one file / concurrency 2, dev server on 5180+ and stop it. Report docs/port/gemini/outbox/148-fight-replay-annotate.report.md
  with 2-3 screenshots (replay of a real 11-26 recording with a drawn route). Branch gemini/148-fight-replay-annotate. FINISH `148 FINISH | ...`.
  Queue: right after 121 (needs its events), before 135b.
