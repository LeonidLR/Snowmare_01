# Codex 156 (L2-L3; Lead reviews) - under fire the operative loops: enters a low cover, steps off for an "open shot", takes cover again... (designer playtest 2026-11-27 ~10:56, recurring)

Formation under fire; the designer ordered the Commander into a low cover. He ran up and kept turning, never settled. Log (play copy, excerpt next
to this file: 156-cover-open-shot-loop-excerpt.log, 08:55-08:57 UTC) shows a ~0.6-1 s loop between two automatic rules:
  [UnderFire] Commander: takes cover against the flash 35 m off (line blocked) -> [Cover] to the low cover at (-7578, 12995) at a sprint ->
  entered / settled -> [Cover] steps off the wall for an open shot at BP_Operative_C_15 (in front of the cover) -> left the cover (open shot:
  target in front of the cover) -> [UnderFire] takes cover again at (-7597, 12995) -> ... (slots alternate -7578 / -7597; RpgChoice also flips
  m16 / rpg7; later "left the cover (critically wounded)").
The two rules disagree about the same enemy: UnderFire says the line is blocked (the cover protects), the open-shot rule says the target is in
front of the cover (it does not). Fix the root cause so both use ONE consistent "which side of this cover is the threat / does it block the
line" answer, and add hysteresis so an operative cannot flip cover <-> open shot more than once per N seconds. A PLAYER cover order must not be
overridden by the automatic open-shot step (he may shoot from the cover / corner, not walk off it) unless the player's order is done.
- Unit tests for the side / blocking decision and the hysteresis; a smoke (full name; extend an existing cover / under-fire smoke, e.g.
  CoverSwitchLakeSmoke, CoverIndoorSmoke or a new one) with an enemy placed where the old rules flip -> he stays in the cover >= 5 s.
- Touches the same area as Codex 153 (cover double-click) and Gemini 135b (corner visuals, lane CodexTactics-135b): base on origin/main after
  153 is merged if it is, keep the change in the combat / cover decision code, report overlaps.
- Lane C:/Work/Lanes/CodexTactics-156, branch codex/156-cover-open-shot-loop from origin/main. Build via Scripts/build.ps1 -Remote worker, Smart
  tests, mapped smokes. Report docs/port/codex/outbox/156-cover-open-shot-loop.report.md; post "156 MERGE-READY | <sha> | ..." via
  Scripts/Tools/agent_chat.py --as codex --to claude. No main / push / maps / .uasset / Config / VFX / designer JSON. 10 %: commit, LIMIT, stop.
