# 154 — Fight Replay Notes UX (Report)

- **Branch / Worktree**: `gemini/154-replay-notes-ux` (`C:\Work\Lanes\CodexTactics-154`)
- **Commits**: `cd1616b7` (plan), `e1bb2429` (implementation, tests, docs), `612e4990` (multi-round respawned operative ID -> squad role mapping)
- **Screenshots** (captured against an isolated temp-dir copy of `2026-11-27_L_Gen_Depot_1_X6_085013`, never modifying `Saved/Demos`):
  - [154-replay-notes-ux-ui.png](docs/port/gemini/outbox/154-replay-notes-ux-ui.png) — loaded Round 2 at `t=260 s` showing Russian role names (`Командир`, `Инженер`, `Медик-сапёр`), **Who** defaulted to selected `Инженер`, and **Tag** dropdown (`прочее`).
  - [154-replay-notes-ux-dedup.png](docs/port/gemini/outbox/154-replay-notes-ux-dedup.png) — after clicking **Remove duplicates** (`51 -> 31`) and selecting/dragging the zone annotation.

## Claims & Evidence

| Claim | Evidence |
|---|---|
| Move drawings as a whole (comment pin, arrow, zone, route, polyline) while preserving vertex/endpoint handle editing | `translatePoints` in `Tools/TrainingGround/src/replay/model.ts:179-182`; whole-shape drag + wide hit targets + per-vertex handle drag in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:122-164`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:194-195`; unit test `Tools/TrainingGround/src/replay/replay.test.ts:117-119`; live CDP drag verified in `task-140` (`DRAG_ZONE: before ["-17773","1750"] -> after ["-16685","1070"]`). |
| Prevent label leaks and duplicate placements; add **Remove duplicates** button keeping pointed entries | `deduplicateAnnotations` in `Tools/TrainingGround/src/replay/model.ts:184-212`; `add()` clears `label`, enforces `e.button === 0`, non-zero drag length, and 350 ms double-click guard, and `chooseTool()` clears `selected`/`label` in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:91-120`; **Remove duplicates** button in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:210`; unit tests `Tools/TrainingGround/src/replay/replay.test.ts:121-155` (including temp-dir copy of real 51-note file); live browser dedup `51 -> 31` (`Removed 20 duplicate(s)`). |
| Show operative role display names from the recording across all rounds (keeping IDs in JSON) and default **Who** to selected / clicked operative | `squadMemberMap`, `squadUnitIds`, `unitDisplayName`, `selectedReplayUnit` in `Tools/TrainingGround/src/replay/model.ts:100-177`; clickable map units + active halo + **Who** dropdown in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:69-83`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:186-192`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:207`; unit test `Tools/TrainingGround/src/replay/replay.test.ts:157-189`; live CDP check `WHO_INFO_R2: {"value":"BP_Operative_C_7","options":[...,{"value":"BP_Operative_C_6","text":"Командир"},{"value":"BP_Operative_C_7","text":"Инженер"},{"value":"BP_Operative_C_8","text":"Медик-сапёр"},...]}`. |
| Per-note `tag` (`хорошо`, `ошибка`, `как надо`, `баг`, `прочее`) with backward-compatible `codex.demo_notes/1` validation and `demo_notes.py` tag output/grouping | `NOTE_TAGS` and `validNotes` in `Tools/TrainingGround/src/replay/model.ts:6-9`, `Tools/TrainingGround/src/replay/model.ts:88-98`; **Tag** dropdown and list badge in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:207-211`; `TAG_ORDER`, `decode()` (`by_tag`, `a['tag']`), and `markdown()` grouping in `Scripts/Tools/demo_notes.py:21`, `Scripts/Tools/demo_notes.py:151-245`; tests `Tools/TrainingGround/src/replay/replay.test.ts:191-198` and `Scripts/Tools/test_demo_notes.py:105-132`. |
| Round remains stored per note; colour stays free for the designer; docs updated | `round: round?.round` and free colour picker preserved in `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:105`, `Tools/TrainingGround/src/replay/ReplayDepartment.tsx:206`; documentation updated in `docs/systems/fight-replay.md:30-66`. |
| All Node replay tests (`--test-concurrency=2`), TypeScript check, and Python `demo_notes` unit tests pass after final code edit (`612e4990`) | `node --test --test-concurrency=2 src/replay/replay.test.ts`: 13/13 passed; `npx tsc --noEmit`: exit 0; `python -m unittest discover -s Scripts/Tools -p test_demo_notes.py -v`: 15/15 passed. |

## Verification Output

```text
> node --test --test-concurrency=2 src/replay/replay.test.ts
✔ late round marks appear on their target fight, preserving call time; footer is legacy only (5.2679ms)
✔ legacy playback uses game duration, reports damaged lines, preserves round results (1.1744ms)
✔ interpolation stays within round and does not bridge respawn (0.6795ms)
✔ 121 exact who/whom killer/victim/op schema, null victim and state evidence (0.9178ms)
✔ smooth drawing retains endpoints and persisted samples validate (1.5057ms)
✔ recording paths reject traversal (0.6138ms)
Real recordings: 19, timed rows: 26991
✔ API list, read-only recording, atomic save/reload and schema/origin rejection (238.2893ms)
✔ all locally available real recordings parse with finite frame positions (330.0118ms)
✔ translatePoints shifts 2D and 3D coordinates for whole-shape dragging (0.4599ms)
✔ deduplicateAnnotations keeps pointed note over time-only duplicate within 2s and preserves distinct points (0.8801ms)
✔ real 51-note designer session copies to temp dir, validates legacy schema, and deduplicates cleanly (12.9498ms)
✔ role display names map squad roles to Russian names while preserving IDs and selection defaults (1.4063ms)
✔ tag field validates allowed values and maintains backward compatibility for untagged notes (0.3109ms)
ℹ tests 13
ℹ pass 13
ℹ fail 0

> npx tsc --noEmit
(exit 0)

> python -m unittest discover -s Scripts/Tools -p test_demo_notes.py -v
Ran 15 tests in 0.047s
OK
```
