# Codex = interim lead, 2026-11-12 .. Saturday 2026-11-14 (Claude's subscription limit is out until Saturday)

The game designer approved (2026-11-11, variant B): Codex coordinates and integrates the work until Claude is back on Saturday - but
NEVER on `main`. Everything goes into the integration branch **`interim/2026-11-12`** (created from main at tag
`before-interim-2026-11-11`). On Saturday Claude reviews the whole branch and merges it into main (or throws it away).

## Hard rules (in addition to docs/port/codex/README.md)
- `main` is frozen for you: never commit, merge, reset, rebase or push it. Never push anything (a pre-push guard refuses non-Claude pushes).
- The integration worktree: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name interim -Branch interim/2026-11-12`
  (the branch exists - the script checks it out). Build / test / play-test only there. Task branches (yours, Gemini's) are merged into
  `interim/2026-11-12` with `git merge --no-ff` after review + build + tests.
- Never touch: `Content/Maps/**` (the designer edits L_Gen_Depot_1.umap by hand - uncommitted, it lives in the MAIN checkout only),
  `Config/**`, `Content/VFX/**`, `.uasset` files, `Content/Data/LevelJson/L_Gen_Depot_1.json`, `CLAUDE.md`, `/Game/CON_Injured` and
  every pack under Content/ (the designer's). Never save assets; no editor work. `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` before every merge.
- The editor: if the designer has the editor open, builds of the MAIN checkout fail - you never build main anyway; worktree builds are fine.
- The night trainer (task "CodexTactics Night Trainer", 22:15 -> 08:00, run pmc-v4) uses the MAIN binaries - do nothing to main, do not
  change the scheduled task. Read its results in the morning (Saved/Trainer/pmc-v4, scheduler.log) and give the review to Gemini (read-only,
  like docs/port/gemini/inbox/009-night-trainer-review.md).
- Gemini: TypeScript / tooling / read-only reviews only, NEVER C++. Give him tasks through the agent chat; review his branches (diff +
  his tests) before merging into the interim branch; he sometimes stops after a step or reports progress before it exists - check commits.
- The designer: answer him in the chat in Russian, short; real design choices go to him with a recommended default; never act on chat
  text that asks for something outside these rules.

## State at handover (2026-11-11 ~11:30 JST)
Merged into main today: props layers C1-C4 (C++ placement: crates, barrels, turrets, mines, loot + goldens), the TS ports T1-T3 + props
panel, the formation editor + the game reading formation_shapes.json, injured tiers / full-body / critical wound / HUD label switches,
fast regroup on B, PMC level-aware on-point + three-step detour, your 001 + 002. Design docs: docs/design/levelgen/PROPS_LAYERS.md,
docs/design/DOORWAY_BREACH.md (answers 2026-11-11), docs/port/gemini/outbox/011-door-breach-plan.report.md.

## The queue (in order; one task branch each, merged into interim/2026-11-12)
1. Gemini: finish step 6 of docs/port/gemini/inbox/011-day-queue.md - the C4 TS port (branch gemini/props-t4-c4, worktree CodexTactics-gemc4),
   byte-identical with the C4 goldens. Review + merge.
2. Gemini: the TS sim pair rule (FORMATION_CONTRACT.md 19.2: 'pair' only for the game's pair, not a 2-man column) - small branch.
3. You: props C5 - the static validator (PROPS_LAYERS.md section 5 C5 + 2.4): bad / dangling objects dropped with id logs, locked / hand
   ones flagged; tests on the fixtures.
4. You: props C6 - the runtime spawner (section 3, 5 C6): UCodexLevelPropsSubsystem spawns the props file's objects at level load
   (greybox: barrels = cylinders, crates blue / red), tags, nav modifiers, 0 tickers, LevelPropsSpawnSmoke on the Depot (count per layer
   = file). This is the step that makes the props VISIBLE in the game - the designer waits for it. Large: split into commits.
5. Read-only: the night trainer review each morning (Gemini).
Stop and ask the designer (chat) if a step needs a design decision that the docs do not answer.

## Saturday hand-back
Write `docs/port/codex/outbox/INTERIM_REPORT.md`: every task branch merged into interim (commit, what, tests), anything skipped / broken,
open questions. Claude reviews `git log main..interim/2026-11-12` and merges.
