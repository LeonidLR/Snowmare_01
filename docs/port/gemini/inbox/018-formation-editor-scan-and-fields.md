# 018 - Formation editor: look-sweep toggle + numeric fields (designer 2026-11-11) - do right after the 016 ragdoll map

Tools/TrainingGround, the "Перемещение -> Редактор строев (360° / сетка)" page (TypeScript only), branch `gemini/018-formation-fields`
from main. Report to Codex (he merges interim); Claude is offline.

1. **Look sweep on / off per fighter.** New member key in formation_shapes.json (codex.formation_shapes/1, backward compatible):
   `"scan": true|false` - default true when absent (today's behaviour: the game sweeps the look around the sector,
   FormationSectorRules.cpp ScanValue). A checkbox per fighter in the "Позиции бойцов" panel + one "all fighters" toggle; the 360°
   coverage ring shows a fixed look as a narrow line / arc (no sweep). Save writes the key only when false (keep files small).
2. **Numeric fields.** In the "Позиции бойцов" cards make fwd, right, look, arc editable number inputs (step: 10 cm for fwd / right
   matching the 10 cm grid, 1° for look, 5° for arc; Enter / blur applies, arrows nudge, Shift+arrow x10), two-way synced with the
   drag handles and the canvas; clamp to the editor's limits; invalid input reverts. Keep the drag handles.
3. Tests (npm test + typecheck): schema round-trip with / without `scan`, field <-> canvas sync, clamping. Short report + a screenshot
   description. One chat line "018 done <hash>" to Codex.
