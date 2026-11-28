# 154b — Fight Replay Notes Filter & Round Splitting (Report)

- **Branch / Worktree**: `gemini/154b-replay-notes-filter` (`C:\Work\Lanes\CodexTactics-154b`)
- **Screenshot** (captured on isolated temp-dir copy of `2026-11-27_L_Gen_Depot_1_X6_104238`, never modifying `Saved/Demos`):
  - [154b-replay-notes-filter.png](docs/port/gemini/outbox/154b-replay-notes-filter.png) — Round 2 picked (`Раунд 2 · ничья · 10:00`), clock `07:39 / 10:00`, Round-2-only units/events/timeline/4 notes, selected note highlighted with white halo and full wrapped label, compact snippets on other Round-2 notes, filter row (`Текущий раунд` / `Все`, `Только теги`, `Подписи на карте`), and `◀ Пред.` / `След. ▶` round controls.

## Claims & Evidence

| Claim | Evidence |
|---|---|
| Split sector recordings into rounds (`round_start` / `round_end`), ignore `pmc_relocated`, and fall back to 1 round for legacy recordings | `ReplayRound`, `roundResultLabel`, `splitRounds`, `annotationRound` in `Tools/TrainingGround/src/replay/model.ts:214-268`; unit test `Tools/TrainingGround/src/replay/replay.test.ts:205-237`. |
| Round picker (`Весь бой` / `Раунд N · результат · MM:SS`, `◀ Пред.` / `След. ▶`, `PageUp` / `PageDown`) above timeline | State & navigation `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:25-126`, round picker bar `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:324-334`, CSS `Tools/TrainingGround/src/replay/replay.css:1`; live CDP check `R2_START` and `NAV_KEYS` (`PageDown` -> R3, `PageUp` -> R2). |
| Round mode scopes timeline (`00:00` = spawn), scrub (`step="any"`), play, units (including initial spawn pose before first snap), events, notes, and auto-assigns `round` on new notes | `firstRoundFrame` / `renderT`, `belongsToActiveRound`, `events`, `timelineEvents`, `add()` in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:127-166`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:217-220`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:335-336`; live CDP `R2_START` (`clock: "00:00 / 10:00"`, `listCountR2: 4`, `rangeMinMax: ["25.474998008459806","625.4800545834005"]`). |
| Default `Весь бой` note rendering (current round full, other rounds faint without labels), filter row (`Текущий раунд` / `Все`, `Только теги`, `Подписи на карте`), and hover/select full-label highlight | `evaluateAnnotations`, `wrapLabelLines`, `compactMapLabel` in `Tools/TrainingGround/src/replay/model.ts:270-367`; `mapNotes` / `listNotes`, `shape(..., halo)`, filter controls, and hover/focus list in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:167-196`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:267-276`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:305-320`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:347-362`; unit test `Tools/TrainingGround/src/replay/replay.test.ts:239-322`. |
| Docs updated and self-review (`pro` subagent) + Jev boundary audit passed | `docs/systems/fight-replay.md:17-19`, `docs/systems/fight-replay.md:42`; all 4 MEDIUM and 5 LOW findings from `pro` review resolved before final commit; `typesafe_triage.py --audit-diff --agent gemini` PASS (0.1/4). |
| All Node replay tests (`--test-concurrency=2`), TypeScript typecheck, and Python `demo_notes` tests pass after final code edit | `node --test --test-concurrency=2 src/replay/replay.test.ts`: 15/15 pass (including 21 real `.jsonl` recordings); `npm run typecheck`: exit 0; `python -m unittest discover -s Scripts/Tools -p test_demo_notes.py -v`: 15/15 pass. |

## Self-Review Fixes (from `pro` subagent review)

- Fixed range slider `step="any"` (`ReplayDepartment.tsx:336`) so short or non-0.1s-aligned rounds reach exact `viewEnd`.
- Fixed initial round render (`firstRoundFrame` / `renderT` at `ReplayDepartment.tsx:129-135`) so units display at `00:00` even when the first `snap` arrives ~0.2–1.0 s after `round_start`, without altering `unitsAt` invariants.
- Filtered `belongsToActiveRound` (`ReplayDepartment.tsx:149-156`) to stop next-round `round_start` / `round_mark` leakage on unended rounds.
- Added `resetDrag()` to `selectRound()` (`ReplayDepartment.tsx:66-85`) and `hoveredNoteId` cleanup (`ReplayDepartment.tsx:194-196`).
- Fixed duplicate `footer.result` in round-mode transport status, added `comment` pin highlight halo (`shape(..., halo)`), auto-included tag when adding under an active tag filter, allowed `PageUp`/`PageDown` while focused on range slider, and added empty-list hint (`Показать все`).

## Verification Output

```text
> node --test --test-concurrency=2 src/replay/replay.test.ts
Real recordings: 21, timed rows: 29103
ℹ tests 15
ℹ pass 15
ℹ fail 0

> npm run typecheck
(exit 0)

> python -m unittest discover -s Scripts/Tools -p test_demo_notes.py -v
Ran 15 tests in 0.066s
OK
```
