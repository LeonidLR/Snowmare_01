# Task 036 — x2 obstacle height classes

Branch: `codex/036-x2-heights` (agents worktree). Working-data commit: `4d37954754f6f05f9145f96c4529c4a5a2f81978`.
Router: level 2 | gpt-6.1-sol | pass (root's Jev classification 0.95; no local model).

Updated ONLY `Content/Data/LevelGen/TestFixtures/L_Gen_Test_X2.layout.json`, `Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1_x2.json`, and the tiny adapter `Scripts/Tools/apply_x2_height_classes.py`. The original tool's public `rework()` remains the transformation owner; class values come from `Content/Data/LevelGen/obstacle_heights.json`.

Why adapter: `Scripts/Tools/apply_depot_height_classes.py` hardcodes the designer Depot path and its original 11 lowered fence IDs. Direct reuse on an expanded map would raise 46 already-correct 135 cm expansion fences to 330, raise 8 already-correct 140 cm expansion cut walls to 230, and flip 26 already-correct 85 cm crates to 140. Existing generator evidence: `generate.ts:93` chooses high_fence_over; `:102` chooses high_mantle for cut walls; `:679-680` chooses low_mantle for crates; `expand.ts:233` chooses high_mantle for cut extensions; `blockTemplates.ts:211` chooses high_fence_over. Adapter preserves these nominal classes by kind (including 140 cm crates, 230 cm parapets and 330 cm fences for idempotence), while retaining the original tool's fence whitelist and clearance check for the legacy 250 cm fences. No generator, source tool, class configuration or golden was edited.

Commands (cwd repository root; CODEX_AGENT=codex, PYTHONIOENCODING=utf-8):

```text
python -B Scripts/Tools/apply_x2_height_classes.py --dry-run
python -B Scripts/Tools/apply_x2_height_classes.py
python -B Scripts/Tools/apply_x2_height_classes.py --check
```

Dry-run inspected before applying. Check PASS on both layouts after applying, with exact byte equality on a repeated transformation. Fixture LF and accepted CRLF preserved (19664 lines each).

Each layout has the same 430 covers / 268 walls before and after. Exactly 246 scalar substitutions/file: 206 cover z sizes and 40 wall heights; git numstat 246 insertions / 246 deletions each. No coordinate, ID, count, route, parameter, bounds, placement, roof, metadata or other JSON field changed (recursive comparison after restoring only height scalars to HEAD). The two JSONs remain semantically identical. Bounds remain min [-8750,-12500], max [8750,12500], 175 x 250 m; params.size_m remains [177,124] (original base params).

| Kind | Before count x height cm | After count x height cm |
|---|---|---|
| boiler | 16x160 + 4x140 | 20x140 |
| crate | 100x110 + 53x85 | 103x85 + 50x140 |
| wreck | 69x150 + 24x140 | 93x140 |
| jersey | 8x90 + 6x95 | 14x95 |
| sandbag | 13x100 + 14x95 | 27x95 |
| fence | 34x250 + 46x135 | 23x330 + 57x135 |
| cut_wall | 6x220 + 8x140 | 6x230 + 8x140 |
| barrel (untouched) | 68x90 + 53x95 | same |
| silo (untouched) | 2x1200 | same |

Original 100 legacy crates split exactly 50/50 into 85/140. Original 34 legacy fences use tool's unchanged 11/23 whitelist split into 135/330.

Validation: direct existing `validateGenLayout()` and `checkLayoutRoutes()` from Training Ground run before and after on each fixture: zero schema errors; R_main (mandatory), V1_gallery, V2_north_blocks and V3_cut all completable (8/8 checks before and after). This is the levelgen route model, not a rebuilt UE navmesh.

Focused existing tests (cwd Tools/TrainingGround):

```text
node --test src/levelgen/levelgen.test.ts src/levelgen/levelgenV2.test.ts src/levelgen/levelgenV2Expand.test.ts src/levelgen/regionRegen.test.ts src/levelgen/props/obstacleHeightGate.test.ts
```

Final run: PASS 65/65, zero fail/cancelled/skipped/todo, 110.823 s (prior pass 65/65, 42.536 s). Final last 15 lines:

```text
✔ regen locks: a locked district survives a re-roll whose zone overlaps it; nothing new intrudes; stored + reloaded (970.835ms)
✔ regen restore: a building whose fresh twin cannot be placed (touches a lock) is kept, the route through it stays completable (461.4333ms)
✔ regen schema: old files (no regen block) still load; a newer regen version is refused (149.4511ms)
✔ regen route gate: a broken route is rejected and retried with the next seed; all broken -> the map is unchanged (1138.6607ms)
✔ regen seams + heights: roads and the canal are untouched by a whole-map re-roll; new obstacles use the height classes (269.0815ms)
✔ regen splice: old points outside + fresh points inside, links re-indexed, border legs without links (1.0418ms)
✔ regen history: >= 20 undo steps, redo, a new step clears redo, the limit trims the oldest (1.0685ms)
ℹ tests 65
ℹ suites 0
ℹ pass 65
ℹ fail 0
ℹ cancelled 0
ℹ skipped 0
ℹ todo 0
ℹ duration_ms 110823.412
```

Staged `python -B Scripts/Tools/typesafe_triage.py --audit-diff --agent codex`: PASS, protected-path check PASS, Jev risk 0.2/4 (3 files). `git diff --cached --check`: PASS.

Unchanged standard Depot layout verified against HEAD (newline-normalized git blob SHA256 `15665771e629865f5249b96ff4af2a4e3d295a28494d9db1c35fdb89be618d86`). Frozen golden `Content/Data/LevelGen/props/fixtures/golden_depot.layout.json` verified against HEAD (`0ed64c513f769150a7b2252f14a24f2dfa97e5176512e85af15528ffd2fe8dcc`). One initial golden verification used a mistaken Tools/TrainingGround path and failed to resolve; corrected to the actual Content path and passed without any golden writes.

No editor/map build, source/engine/config/assets changes, main checkout writes, push or merge. Claude must rebuild L_Gen_Test_X2 and verify runtime mantle links. Root reviewed the adapter, preserved standard classes and reported validation, then ran the required final audit before this report commit.
