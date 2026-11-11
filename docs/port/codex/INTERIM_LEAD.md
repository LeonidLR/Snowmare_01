# Codex = interim lead, 2026-11-12 .. Saturday 2026-11-14 (Claude's subscription limit is out until Saturday)

The game designer approved (2026-11-11, variant B): Codex coordinates and integrates the work until Claude is back on Saturday - but
NEVER on `main`. Everything goes into the integration branch **`interim/2026-11-12`** (created from main at tag
`before-interim-2026-11-11`). On Saturday Claude reviews the whole branch and merges it into main (or throws it away).

## Hard rules (in addition to docs/port/codex/README.md)
- `main` is frozen for you: never commit, merge, reset, rebase or push it. Never push anything (a pre-push guard refuses non-Claude pushes).
- The integration worktree ALREADY EXISTS: `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics-interim` on
  `interim/2026-11-12` (from main 8b0bb579 = tag `before-interim-2026-11-11`). Build / test / play-test only there. Task branches (yours, Gemini's) are merged into
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
1. (Done 2026-11-11 by Claude: the C4 TS port is already merged in main.)
1b. PRIORITY (the designer's last task 2026-11-11): stealth mode - docs/port/codex/inbox/003-stealth-mode.md. Do it right after the
   small pair-rule task (2), before C5 / C6.
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

## Tools you may use (designer 2026-11-11)
- **Jev (TypeSafe)**: `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` (mandatory before merges), `Scripts/test.ps1 -Smart`
  (impact-based test selection), `python Scripts/Tools/typesafe_triage.py --telemetry <runs.jsonl>` for trainer / bot logs (never read raw
  logs into context). See .claude/skills/typesafe-ai for the API.
- **Anthropic API** (the designer's credit, NOT Claude's subscription): `python Scripts/Tools/api_task.py --model claude-sonnet-5-5
  --effort medium --prompt-file <brief> --files <files...> --out <answer.md> --max-tokens 12000 --label codex-<what> --budget-usd 30`.
  Load the key into the process only: `$env:ANTHROPIC_API_KEY = [Environment]::GetEnvironmentVariable('ANTHROPIC_API_KEY','User')` -
  NEVER print it. `--budget-usd 30` is the hard cap on the WHOLE ledger (Saved/Orchestrator/api_spend.jsonl, ~14.6 $ spent now), so you
  have ~15 $ until Saturday. Use it for second-opinion reviews of diffs (Sonnet, ~0.05-0.2 $), Gemini branch reviews, and patch drafts
  for well-scoped C++ (split big patches into 2-3 file calls; Opus only for design). Always `--label codex-...`. List the spend in
  INTERIM_REPORT.md.
