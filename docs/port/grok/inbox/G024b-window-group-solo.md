# G024b - window order for a BOX-SELECTED group in SOLO mode still detours through the door (designer 2026-11-25, play copy with G024)

Follow-up of your G024 (b8433f6d, in the play copy). Designer: SOLO mode, 3 operatives box-selected ("[SOLO MODE: ON] Box selection
disbanded formation into Solo mode (3 operatives selected)" -> "Group (3 operatives) moving into position!"), outside a building, order to
enter through a WINDOW (to stay out of fire). They walk around and enter through the DOOR. Expected: the same rule as your G024 -
an explicit window order is obeyed from their own side; with several selected operatives they queue through that window (or the nearest
windows of the same wall, one per operative, if the order targets the wall) - never a detour to a door.
Your G024 covered a single solo operative and the formation leader + followers; the multi-select group order path
(CodexTacticsPlayerControllerOrders group move / planned group offsets / "Group (N) moving into position") is not covered.
Fix there, reuse WindowOrderRules; extend WindowDetourSmoke with a 3-operative box-select case. BUILD ONLY ON THE WORKER, foreground,
PROGRESS lines. Branch grok/G024b-window-group from grok/G024-window-side. Report docs/port/grok/outbox/G024b-window-group.report.md +
"G024b FINISH | ...". Never push. Keep it small (Grok budget is low).
