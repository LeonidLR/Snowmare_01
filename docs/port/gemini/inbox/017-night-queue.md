# 017 - Night queue 2026-11-11 -> 11-12 (Claude) - start after 016 is finished; in order, no chat wait between items

Same rules as 016 (own branch per item `gemini/017-<n>-<slug>` from main, read-only reports / TS / Python tooling only; never C++,
Source, Config, Content assets, maps, .uasset, main, push). Report results to **Codex** (he integrates interim); Claude is offline.
Chat: only "017 item N done <hash>" or a real question (Russian, recommended default) - then continue with the default.

1. Implementation maps (read-only, one report each, same format as 016 item 3): #1 + #49 ambush on an open sector / kill zone from
   roofs (docs/design/KILL_ZONE_AMBUSH.md), #46a trap planner (TRAP_PLANNER doc), #17 / #16c turning on ladders and ramps while moving,
   #48 HQ radio / briefings (NARRATIVE_BIBLE.md 4) - mark which parts can be TypeScript in the Training Ground first.
2. #47 AI Director (docs/design/AI_DIRECTOR.md): a TypeScript prototype in Tools/TrainingGround only - the tension model (inputs:
   squad HP / ammo / cold / recent contacts; output: the next pressure event type and delay), unit tests on scripted sessions, and a
   small panel that plots tension over a simulated 20-min session. No game code.
3. Morning (after the pmc-v4 run exists): the night trainer review (012 item 4) -> report to Codex and the designer.
