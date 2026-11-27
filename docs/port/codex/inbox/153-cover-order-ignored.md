# Codex 153 (L2-L3; Lead reviews) - a cover order (double-click on a cover) is silently ignored (designer playtest 2026-11-27 ~09:12, play copy, X6, Solo)

Two cases, both with Engineer (Solo, 1 of 2 selected), low covers (StaticMeshActor_2409 and its neighbour), log excerpt next to this file
(153-cover-order-ignored-excerpt.log, play copy Saved/Logs/CodexTactics.log from line 7990):
1. Engineer sits in a low cover; the designer double-clicks ANOTHER low cover: the ghost appears, the second click is logged
   "[ClickPreview] second click ... (double click): carries out the cover ghost" - and then NOTHING (no "move to", no "[Cover] ... to the low
   cover", no refusal message). See 07:13:08 and 07:13:56. Other times the same action works (07:13:03, 07:13:59: "double-click on another
   cover - not in cover -> the ordinary cover order").
2. Engineer leaves the cover on a ground click (07:14:00 "left the cover (move order)", move to (-2996, -8883)); while he walks, the designer
   double-clicks a cover: the ghost shows (07:14:03 first click) but he keeps walking to the old ground point; the new cover order never takes.
Find why "carries out the cover ghost" ends without an order (state check: in cover / leaving cover / moving / ghost for the cover he is
already in / the slot equals his current slot / double-click timing ...). Rule wanted: a cover double-click ALWAYS either replaces the
current order (in cover, moving, leaving cover) or says why not on screen - never silent.
- Unit test for the decision; extend an existing cover smoke (full names, e.g. CoverSwitchLakeSmoke / LowCoverMoveLakeSmoke) with:
  in cover -> double-click another cover -> he goes; walking -> double-click a cover -> he switches to it.
- Note: Gemini 135b (corner visuals in OperativeCharacterCover*.cpp / CornerAim) runs in parallel in C:/Work/Lanes/CodexTactics-135b - keep
  your change in the order / click path, report any overlap.
- Lane C:/Work/Lanes/CodexTactics-153, branch codex/153-cover-order-ignored from origin/main. Build via Scripts/build.ps1 -Remote worker,
  Smart tests, mapped smokes. Report docs/port/codex/outbox/153-cover-order-ignored.report.md; post "153 MERGE-READY | <sha> | ..." via
  Scripts/Tools/agent_chat.py --as codex --to claude. No main / push / maps / .uasset / Config / VFX / designer JSON. 10 %: commit, LIMIT, stop.
