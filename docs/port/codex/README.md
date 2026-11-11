# Codex mailbox (Claude -> Codex, trial from 2026-11-11)

Codex (OpenAI) takes Claude-briefed coding tasks, C++ included, on its OWN branch in its OWN worktree. Claude reviews every branch
(diff + tests + an API review) and is the only one who merges into main. Trial: if a task needs more than two review rounds or breaks
something, Codex goes back to non-C++ work.

| Folder | Who writes | What |
|---|---|---|
| `docs/port/codex/inbox/NNN-<name>.md` | Claude | one task per file: goal, files, rules, the exact checks that must pass |
| `docs/port/codex/outbox/NNN-<name>.report.md` | Codex | what changed (files), why, test / smoke results, open questions |

## Rules (always)
- Read `CLAUDE.md` and the top of `docs/port/HANDOFF.md` first (project rules, the Epic C++ standard, traps).
- Work ONLY in your worktree: `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name cx<task> -Branch codex/<task>`
  from the main checkout `C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics`, then work in `...\CodexTactics-cx<task>`.
- `$env:CODEX_AGENT = "codex"` in every shell. Commit with explicit paths (never `git add -A`), Conventional Commits, small commits.
- NEVER: push, merge, touch `main`, write into the main checkout (except your report in `docs/port/codex/outbox/`), touch
  `Content/Maps/**`, `Config/**`, `Content/VFX/**`, `.uasset` files, `Content/Data/LevelJson/L_Gen_Depot_1.json`, `CLAUDE.md`.
- No editor: never open / save assets. Builds and tests are command line: `Scripts/build.ps1`, `Scripts/test.ps1 -Filter <...>`,
  `Scripts/smoke_parallel.ps1 -Commands CodexTactics.<Name>Smoke` (full names).
- Before your last commit: `python Scripts/Tools/typesafe_triage.py --audit-diff --agent codex` must PASS.
- Keep the change minimal: no refactors, no renames, no new helpers when one exists (search first).
- When done: the report + ONE line in the agent chat:
  `python "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics\Scripts\Tools\agent_chat.py" post --as codex --to claude --topic codex-<task> --text "<task> done: <branch> <commit>, <tests>"`
  then wait for review: `... agent_chat.py wait --as codex --timeout 1800` (repeat on NO_MESSAGES).

## The designer's one-line prompt for Codex
```text
Read docs/port/codex/README.md in C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics and do the lowest-numbered task in docs/port/codex/inbox/ that has no report in docs/port/codex/outbox/, following the README rules exactly.
```
