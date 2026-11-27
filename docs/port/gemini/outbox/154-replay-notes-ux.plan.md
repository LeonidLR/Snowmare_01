# 154: Fight Replay Notes UX — Plan

- **Branch / Worktree**: `gemini/154-replay-notes-ux` (`C:\Work\Lanes\CodexTactics-154`)
- **Base**: `origin/main`

## Files to Touch
- `Tools/TrainingGround/src/replay/model.ts`
- `Tools/TrainingGround/src/replay/ReplayDepartment.tsx`
- `Tools/TrainingGround/src/replay/replay.css`
- `Tools/TrainingGround/src/replay/replay.test.ts`
- `Scripts/Tools/demo_notes.py`
- `Scripts/Tools/test_demo_notes.py`
- `docs/systems/fight-replay.md`
- `docs/port/gemini/outbox/154-replay-notes-ux.report.md`

## Steps
1. **Plan commit**: Record plan and file list; post plan summary to `agent_chat.py`.
2. **Model & schema (`Tools/TrainingGround/src/replay/model.ts`)**:
   - Add `NOTE_TAGS` (`хорошо`, `ошибка`, `как надо`, `баг`, `прочее`) and optional `tag?: NoteTag` on `Annotation`; keep `codex.demo_notes/1` backward-compatible in `validNotes` (missing `tag` still valid).
   - Add `translatePoints(points, dx, dy)` for whole-shape dragging.
   - Add `deduplicateAnnotations(annotations)` matching `(kind, trimmed label, |dt| <= 2s, same/absent points)` and keeping the pointed entry when one has points and another does not.
   - Add `unitDisplayName(replay, id, unit?)` and `selectedReplayUnit(replay, t)` mapping `EOperativeRole` (`Commander` -> `Командир`, `Engineer` -> `Инженер`, `MedicSapper` -> `Медик-сапёр`, `Recruit` -> `Новобранец`, keeping IDs in JSON) and reading `sel`/`lead` from snapshot ops.
3. **Replay UI (`ReplayDepartment.tsx`, `replay.css`)**:
   - **Move drawings**: Dragging any shape (`comment` pin, `arrow`, `zone`, `route`, `polyline`, or its hit stroke/label) in `select` mode translates all points as a whole; white vertex handles still edit individual points (`arrow`, `zone`, `route`, `polyline`). Only commit to undo history if points actually changed.
   - **No duplicates & label clear**: Clear `label` after `add()`; clear `selected`/`label` when switching from `select` to a drawing tool; enforce left-click `e.button === 0`, non-zero drag length for `route`/`arrow`/`zone`, and double-click guard for one click = one note; add **Remove duplicates** button (undoable).
   - **Who**: Show operative role display names in the **Who** dropdown and map labels (storing unit `id` in JSON); make units clickable on the map in `select` mode and default **Who** to the clicked or replay-selected (`sel`) operative.
   - **Tag**: Add tag dropdown (`хорошо` / `ошибка` / `как надо` / `баг` / `прочее`) saved as `"tag"`, loaded on selection (`прочее` fallback for untagged legacy notes), and displayed in the notes list.
   - **Round colours**: Keep `round` stored per note and `colour` free for the designer.
4. **Decoder (`Scripts/Tools/demo_notes.py`, `test_demo_notes.py`)**:
   - Preserve `tag` in `decode()`, print `[tag]` per annotation, and group markdown output (and JSON summary) by tag.
   - Add Python unit tests for tag printing, grouping, and untagged legacy compatibility.
5. **Tests, docs & verification (`replay.test.ts`, `docs/systems/fight-replay.md`)**:
   - Add Node unit tests covering `translatePoints`, `deduplicateAnnotations` (including a temp-dir copy of the 51-note real fixture, never modifying `Saved/Demos`), role display names, default selected unit, and `tag` schema validation (new + legacy).
   - Update `docs/systems/fight-replay.md`.
   - Run `node --test --test-concurrency=2 src/replay/replay.test.ts`, `npx tsc --noEmit`, `python -m unittest discover -s Scripts/Tools -p test_demo_notes.py -v`, capture UI screenshots via headless Edge if feasible, run `gemini_gate.ps1 -Task 154`, write outbox report, and notify Claude via `agent_chat.py`.
