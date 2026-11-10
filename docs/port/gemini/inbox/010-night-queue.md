# 010 - Night queue 2026-11-10 -> 11 (Training Ground, TypeScript ONLY)

Work through the steps IN ORDER. One branch + one worktree per step (from the CURRENT main, `git pull` / branch from `main`
first), `Scripts/Tools/new_agent_worktree.ps1 -Name <name> -Branch <branch>`. Rules: `docs/port/gemini/README.md` - NO C++,
nothing under `Source/`, `Config/`, `Content/Maps/`, no `.uasset`; never write into the main checkout except your reports in
`docs/port/gemini/outbox/`; never push or merge (Claude reviews every branch through the API and merges in the morning).
COMMIT after every finished part of a step (the Antigravity session may stop at any time - committed work survives).
After each step: write `docs/port/gemini/outbox/010-<step>.report.md` (what, files, test counts, screenshots, open questions) and post
ONE line in the agent chat (`python "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics\Scripts\Tools\agent_chat.py" post
--as gemini --to claude --topic night-queue --text "<step> done: <branch> <commit>, <tests>"`). If a step is blocked for more than
30 minutes, write why in its report and go to the next step. The night trainer runs from 22:15 and loads the CPU - that is fine.

## Step 1 - finish 008 (branch gemini/formation-editor, worktree CodexTactics-gemformed)
The 6 review defects in `docs/port/gemini/reviews/008-formation-editor-review.md` (unguarded readBody; unknown keys dropped on shapes /
custom slots; roles must equal rolesFor(count) with the commander at index 0; the pair shape only for 'pair'; custom validation: null
member, arc integer 10..360, CUSTOM_MAX_RADIUS, unique squad; atomic write with a unique temp + cleanup). A test for each. Skip if
already done and committed.

## Step 2 - T2: the props placement port (branch gemini/props-t2)
`docs/design/levelgen/PROPS_LAYERS.md` section 5 T2 and section 7 "TS port notes" (the exact RNG / candidate / filter / shuffle order).
Port `PropsRng` (mulberry32 + fnv1a), the common algorithm, crates + barrels (incl. the new rule key `wall_gap_cm`, crates 5 cm;
barrels: all explosive, 8 per map, clusters 1-3) to `Tools/TrainingGround/src/levelgen/props/`. Use your T1 schema / writer.
ACCEPTANCE (the only one that matters): for every case in `Content/Data/LevelGen/props/fixtures/golden_cases.json`, placing
`golden_in_*.props.json` on its layout reproduces `golden_*.json` BYTE FOR BYTE (LF endings). A test per case; on a mismatch print the
first differing object and the RNG call count so the cause is findable. Do not "fix" the goldens - if you are sure the C++ is wrong,
say so in the report with the evidence.

## Step 3 - HUD labels panel (branch gemini/hud-labels-panel)
The game now reads `Content/Data/UI/hud_labels.json` (schema `codex.hud_labels/1`, 26 bool switches with a Russian `label` + English
`doc`; the game re-reads it live in dev builds). A panel in the Training Ground (where the designer tunes per-level / game settings):
grouped switches (operatives / enemies / world / HUD - group by key prefix or by your judgement), "all on" / "all off" per group, saves
the file through the dev server (only that file, atomic write, keys and comments kept, nothing reformatted). Tests: round-trip, unknown
keys kept, group toggles.

## Step 4 - T5 shell: the props panel (branch gemini/props-panel-shell)
In the level-generator department: load `Content/Data/LevelGen/props/<map>.props.json` (and the fixtures), draw every layer's objects
over the existing layout canvas (crates blue squares, barrels red circles, turrets / mines / loot placeholders), a toggle per layer,
locked objects outlined, a legend; if step 2 landed, a "re-roll this layer" button using the TS port (new seed, locked objects kept),
saving through the dev server with the revision check of section 4.1 (409 on a stale revision). Screenshots in the report.

## Step 5 (morning, only after the night trainer finished, ~09:00 JST) - task 009
`docs/port/gemini/inbox/009-night-trainer-review.md` (read-only review of tonight's run).
