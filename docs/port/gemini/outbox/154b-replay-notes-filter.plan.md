# 154b: Replay Notes Filter & Round Splitting — Plan

- **Branch / Worktree**: `gemini/154b-replay-notes-filter` (`C:\Work\Lanes\CodexTactics-154b`)
- **Base**: `origin/main` (after `154` merge `8af22865`)

## Files to Touch
- `Tools/TrainingGround/src/replay/model.ts`
- `Tools/TrainingGround/src/replay/ReplayDepartment.tsx`
- `Tools/TrainingGround/src/replay/replay.css`
- `Tools/TrainingGround/src/replay/replay.test.ts`
- `docs/systems/fight-replay.md`
- `docs/port/gemini/outbox/154b-replay-notes-filter.plan.md`
- `docs/port/gemini/outbox/154b-replay-notes-filter.report.md`
- `docs/port/gemini/outbox/154b-replay-notes-filter.png`

## Steps
1. **Plan commit**: Record plan and exact file list in `docs/port/gemini/outbox/154b-replay-notes-filter.plan.md` and notify `agent_chat.py`.
2. **Round splitting & note filtering (`Tools/TrainingGround/src/replay/model.ts`, `replay.test.ts`)**:
   - Add `ReplayRound`, `splitRounds(replay)`, `roundResultLabel(result)`, and `annotationRound(a, replay, rounds)`:
     - Split by `round_start` / `round_end` events (`startT` = `round_start.t`, `endT` = matching `round_end.t` or next `round_start.t` or `replay.duration`); `pmc_relocated` events are NOT round boundaries.
     - Legacy recordings without `round_start` events return one round `[0, replay.duration]`.
   - Add `evaluateAnnotations(annotations, replay, rounds, opts)`:
     - In `"Весь бой"` (`selectedRound === 'all'`): default `scope = 'current'` draws current round's notes fully (shape + label when `labelsOnMap`) and other rounds' notes as faint shapes without labels; `scope = 'all'` draws all rounds fully; `tags` filters strictly to selected tags; hovering/selecting a note highlights it on the map with its full label.
     - In round mode (`selectedRound === N`): hides other rounds' notes completely (`hidden`).
   - Add Node unit tests in `replay.test.ts` for round splitting (events, legacy file, `pmc_relocated` non-boundary) and note filter/visibility rules.
3. **Replay UI (`Tools/TrainingGround/src/replay/ReplayDepartment.tsx`, `replay.css`)**:
   - Add round picker bar above the timeline: `◀ Пред.` | `Весь бой` | `Раунд N · результат · MM:SS` ... | `След. ▶`, plus `PageUp` / `PageDown` keyboard navigation.
   - When a round is picked: scope timeline/scrub/play/event ticks/units/events/notes to `[round.startT, round.endT]` with clock `00:00` = spawn (`round.startT`), and assign new notes to that round.
   - Add note filter row in the annotations panel: `[Текущий раунд]` `[Все]` + tag filter buttons (`хорошо`, `ошибка`, `как надо`, `баг`) + `Подписи на карте` toggle (labels always visible in the list).
   - Add list hover/focus/select highlighting on the SVG map (full opacity, wider stroke/halo, rendered on top with full label).
4. **Docs, verification & report (`docs/systems/fight-replay.md`, outbox report & screenshot)**:
   - Update `docs/systems/fight-replay.md`.
   - Run `npm test` and `npx tsc --noEmit` in `Tools/TrainingGround`.
   - Capture live UI screenshot via headless Edge against an isolated temp-dir copy of the real recording + notes (never modifying `Saved/Demos`).
   - Run `Scripts/Tools/gemini_gate.ps1 -Task 154b`, write report, commit, and post `154b MERGE-READY | <sha> | ...` via `agent_chat.py`.
