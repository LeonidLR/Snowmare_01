# 011 - Day queue 2026-11-11 (TypeScript + read-only reviews; NO C++)

Same rules as 010 (`docs/port/gemini/README.md`): one branch + worktree per coding step from the CURRENT main; NO C++ / Source /
Config / Content/Maps / .uasset; never write into the main checkout except your outbox reports (and `Saved/Orchestrator/replay/` for
009); never push / merge; COMMIT after every finished part; after each step an outbox report `011-<step>.report.md` + ONE chat line
(`--topic day-queue`). Blocked > 30 min -> write why, go on. Claude merges after an API review.

## Step 1 - the blocking fix (branch gemini/props-panel-shell) - chat message #82
`server/propsApi.ts resolvePropsFile`: accept only a bare map name /^[A-Za-z0-9_.-]+$/ (no slashes, no '..'), resolve only inside
PROPS_DIR / PROPS_FIXTURES_DIR, assert `path.relative(dir, full)` does not start with '..', PUT refuses fixtures (golden_*) and
anything not ending `.props.json`; a test per refusal. Move `render_props_screenshot.py` to `Tools/TrainingGround/scripts/`.

## Step 2 - task 009 (read-only): the night trainer review
`docs/port/gemini/inbox/009-night-trainer-review.md` - tonight = run pmc-v3 generations written after 2026-11-10 22:15 JST (31 .. 61);
the trainer's own `Saved/Trainer/pmc-v3/report.html`, `leaderboard.json`, `champion_meta.json`, `highlights/` help.

## Step 3 - T3: the fast gate in the Training Ground (branch gemini/props-t3)
`docs/design/levelgen/PROPS_LAYERS.md` section 5 T3 + section 2.4: a nav grid of the layout with the nav-affecting props (crates,
barrels, turrets; not mines / loot) as obstacles widened by the 35 cm agent radius; completability of the mandatory route + every
doorway / gate / ladder foot / top reachable; run on every re-roll in the props panel, show a red "route blocked at <leg>" badge with
the blocking object ids. Tests: a fixture with a crate in a gate fails with the leg named; Depot with the default layers passes.

## Step 4 - door breach: implementation plan against the CURRENT code (read-only)
`docs/design/DOORWAY_BREACH.md` (your spec of 2026-11-04) was written before a week of changes. Re-read the current code
(Source/CodexTactics/**: squad orders, cover / peek / corner hold, formation wall stack, the doorway data of the level generator -
PropsLayoutView doorways incl. gates) and write `docs/port/gemini/outbox/011-door-breach-plan.report.md`: what already exists and can
be reused (file:line), what the spec needs that does not exist, a step list for Claude (each step: files, size S/M/L, acceptance
smoke), the open design questions for the designer (max 6, each with a recommended default). No code.

## Step 5 - read-only bug review of yesterday's C++ merges
Review for real defects only (null derefs, wrong state transitions, frame-order bugs, data keys read but unused, tests that cannot
fail): the 2026-11-10 merges on main - `git log --merges --since=2026-11-10 --until=2026-11-11 main` (injured locomotion + tiers +
critical wound, fast regroup, arena play area / rocket contact / colours / camera, Training Ground pass 1 C++, props C1-C3, analyzer
guards). Report `011-cpp-review.report.md`: each finding with file:line, the concrete failing state, a one-line fix idea, a
confidence. Max 15 findings, the most dangerous first. No code.

## Step 6 - when the C++ C4 lands (Claude posts in the chat): the TS port of C4
Turrets / mines (doorway N and %, crate %, mandatory-route forbid) / loot, byte-identical with the new C4 golden cases (same as T2).
