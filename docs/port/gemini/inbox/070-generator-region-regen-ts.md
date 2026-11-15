# 070 - Level generator v2, stage 1: region re-roll + locks in the Training Ground (TypeScript only) (designer 2026-11-15, board 066)
Суть: на 2D-плане карты дизайнер выделяет рамкой или многоугольником зону и перегенерирует только её, а понравившиеся районы закрепляет.
Реестр: levelgen
Base: origin/main; branch gemini/070-region-regen; worktree CodexTactics-interim (`git switch -c gemini/070-region-regen origin/main`
after 068 is committed). Start ONLY after 067 and 068 are reported.

## Goal (stage 1 of board 066; NO Unreal / C++ / .umap work in this task)
In the TG department "Генератор уровней" (Tools/TrainingGround/src/departments/LevelGenDepartment.tsx, generator in
Tools/TrainingGround/src/levelgen/, API Tools/TrainingGround/server/levelgenApi.ts):
1. Select a zone on the 2D plan by a RECTANGLE or a POLYGON / lasso.
2. "Перегенерировать зону": re-generate only the objects whose CENTRE is inside the shape (buildings always whole), a new seed per click,
   everything outside stays byte-identical. Deterministic: same map + same shape + same seed -> same result.
3. History / undo / redo of re-rolls (at least 20 steps).
4. LOCK a district: a polygon stored in the map's levelgen JSON (new field, versioned schema, old files still load); objects inside a lock
   are never touched by any re-roll (including a re-roll whose shape overlaps the lock).
5. Seam rules: after a re-roll, roads / the canal do not end at the zone border, and the existing route check runs (reject + re-roll up
   to N times if the route breaks; report the count). Reuse the existing route / validation code - find it first.
6. Obstacle heights: new objects use the height classes from the J standard already in the levelgen data (58a7e34e) - do not invent new
   heights.
Not in this task: canvas extension x2/x4/x6, start + goals, building in UE (stages 2-3, later).
## Allowed paths
- Tools/TrainingGround/src/levelgen/**, Tools/TrainingGround/src/departments/LevelGenDepartment.tsx (+ new components next to it),
  Tools/TrainingGround/server/levelgenApi.ts, Tools/TrainingGround tests, the levelgen JSON schema file if there is one,
  docs/port/gemini/outbox/070-*.
- NOT: Content/Data/LevelGen/accepted/** (accepted maps), Content/Data/LevelJson/**, Source/**, Content/Maps/**, any .uasset.
## Steps
1. PLAN commit (070-region-regen.plan.md: where the generator keeps objects, how a partial re-roll plugs in, schema change) -> chat.
   Wait up to 20 min; no answer -> go on.
2. Pure TS functions first (point-in-polygon, select-by-centre, partial regen, lock filter) + vitest/jest tests (use the runner the repo
   already uses - see levelgen.test.ts).
3. UI. 4. Route check at the seams. 5. Gate + report with screenshots of the TG page (before / after a re-roll, a lock) in
   docs/port/gemini/outbox/screenshots/070-*.png.
## Checks (paste outputs)
- `npm test` (or the TG test command from package.json) in Tools/TrainingGround - all green, new tests listed.
- `npm run build` / typecheck green.
- Determinism test: re-roll twice with the same seed -> identical JSON (hash in the report).
- `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 070 -SonnetReview`
## Report (docs/port/gemini/outbox/070-region-regen.report.md, Russian)
| Claim | Evidence |
|---|---|
