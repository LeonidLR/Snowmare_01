# 082 - Props panel / props autogen ignore rotated buildings (Claude, 2026-11-17) - SMALL-MEDIUM, TypeScript only (Antigravity day, moved from CLI 2026-11-18)
Base: main. Branch gemini/082-props-rotation. Worktree CodexTactics-gemday. Limit ~1.5 h.
Why (LEVELGEN_LAYERS_REPORT + designer 11-17): the level generator can rotate buildings (max-angle setting), but the props code
(Tools/TrainingGround/src/levelgen/props/**: the props panel / tab T5, the fast gate, autogen.ts from your 076) treats every building as an
axis-aligned box - near a rotated building props can land inside the walls or too far from them.
Do (items 1-2 first):
1. Find every place in src/levelgen/props/** that tests a point against a building / zone footprint as an AABB; use the building's real
   (rotated) polygon - reuse the existing rotated-footprint helper of the layout / layers code (grep "rotation" / "yaw" / "footprint" in
   src/levelgen) - do not write a second one.
2. Tests: a 30 deg and a 45 deg rotated building - a prop inside its rotated footprint is rejected, one just outside the rotated wall but
   inside the old AABB is accepted; autogen on a layout with rotated buildings puts no prop inside a building; the existing 441+ TG tests stay
   green; npm run typecheck.
3. (if time) the 2D props layer draws footprints rotated.
Allowed paths: Tools/TrainingGround/src/levelgen/props/**, its tests, docs/port/gemini/outbox/082-*. No C++, no Content/**.
FINISH message as usual (status | last commit | done | NOT done | needs Claude | gate | report).
