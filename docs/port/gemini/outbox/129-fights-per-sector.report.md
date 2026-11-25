# Task 129 — AI Training Tab: Fights per Sector (X6)

## Changes Made
- **`Tools/TrainingGround/src/aiTraining/trainingTypes.ts`**: Added `SectorFightRow`, `SectorFightStats`, and optional `sectorStats` on `TrainingData`.
- **`Tools/TrainingGround/server/trainingApi.ts`**:
  - Added `resolveMainCheckoutRoot(root)` (reads `.git` worktree file when launched from `C:/Work/Lanes/*` to locate main checkout's `Saved/Demos`, falling back cleanly to `root` in isolated test dirs).
  - Added mtime+size-cached `.jsonl` parser (`parseDemoFileStats` + `readSectorFightStats(root)`) reading only `header`, `round_end`, and `footer` lines (`NON_SNAP_TYPE_RE` skips all `"snap"` lines without `JSON.parse`), tolerating truncated or malformed lines cleanly.
  - Aggregates X6 (`L_Gen_Depot_1_X6`) sessions, completed rounds, wins/losses/draws, total minutes, and last played date across sectors `1..6`, older unlabelled sessions as `"без сектора"`, `"всего"` totals, and marks the lowest-round sector with `fewest: true` (`"сыграйте сюда"`).
  - Refreshes automatically on every `GET /api/ai-training` (tab open), backed by file mtime cache.
- **`Tools/TrainingGround/src/departments/AiTrainingDepartment.tsx`**: Added `SectorStatsTable` displaying sectors `1..6`, `"без сектора"` (when present), `"всего"`, and highlighting the fewest-rounds sector with `"сыграйте сюда"`.
- **`Tools/TrainingGround/src/aiTraining/sectorFightStats.test.ts`**: Added Node test coverage for complete, aborted, no-sector, truncated/corrupted lines, non-X6 exclusion, fewest-round selection, `.git` worktree main-root resolution, and mtime cache invalidation.

## Verification
- `node --test --test-concurrency=2 src/aiTraining/sectorFightStats.test.ts src/aiTraining/trainingApi.test.ts`: **4/4 tests PASS** (`404.7ms`).
- `npm run typecheck` (`tsc && tsc -p tsconfig.node.json`): **PASS** (exit 0).
- Live dev server verification on `http://localhost:5185` (stopped afterwards):
  - `"Тренировка ИИ"` rendered live data from main `Saved/Demos/*.jsonl`: Sector 1 (`1 sess, 0 rounds, 5.9 min`, highlighted `"сыграйте сюда"`), Sector 2 (`1 | 2 | 2/0/0 | 12.7 min`), Sector 3 (`2 | 7 | 3/4/0 | 19.3 min`), Sector 4 (`1 | 5 | 4/1/0 | 21.8 min`), Sector 5 (`1 | 4 | 3/0/1 | 40.0 min`), Sector 6 (`1 | 5 | 1/4/0 | 15.6 min`), `без сектора` (`3 | 0 | 0/0/0 | 4.9 min`), `всего` (`10 | 23 | 13/9/1 | 120.1 min`), zero console/HTTP errors.
