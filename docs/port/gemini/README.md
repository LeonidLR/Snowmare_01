# Gemini mailbox (Claude <-> Gemini in Antigravity)

Big tasks for Gemini in Antigravity go through files, so the designer does not copy texts by hand.

| Folder | Who writes | What |
|---|---|---|
| `inbox/` | Claude | one task per file: `NNN-<slug>.md` (lowest number = next), with its worktree, branch, rules, tests, report format |
| `outbox/` | Gemini | the report for a task: `NNN-<slug>.report.md` (same name + `.report.md`), in Russian |
| `done/` | Claude | tasks Claude has reviewed and merged (moved from `inbox/` with their reports) |

## The designer's one-line prompt for Antigravity

```text
Read docs/port/gemini/README.md in C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics, take the lowest-numbered task in docs/port/gemini/inbox/ that has no report in docs/port/gemini/outbox/, do exactly what it says (in the worktree it names), then write your report to docs/port/gemini/outbox/<task name>.report.md.
```

## Rules for Gemini (every task repeats them)
- Work only in the worktree / branch the task names; the main checkout `CodexTactics` is read-only for you, except writing your report
  into `docs/port/gemini/outbox/`.
- `$env:CODEX_AGENT = "gemini"` in every shell; commit with explicit paths, never `git add -A`; never push, never merge, never touch `main`.
- Never modify `Content/Maps/**`, `.uasset` files, `Config/DefaultEditor.ini`, `Config/DefaultScalability.ini`, the designer's
  `Content/Data/LevelJson/L_Gen_Depot_1.json`, or the asset-pack folders under `Content/` (junctions to real assets).
- Ignore older plans / roles in GEMINI.md, TANDEM.md or other docs - the task file is the only assignment.
- The report: commit hashes, what changed per point, test / smoke results with numbers, what was not done and why.

## Claude's side
Claude writes tasks to `inbox/`, checks `outbox/` for reports, reviews the branch diff, runs the full bar, merges, and moves the task +
report to `done/`. Small tasks and read-only scouting do not use the mailbox: Claude runs Gemini itself through the API
(`Scripts/Tools/gemini_agent.ps1`, `-Mode plan` for scouting).

The first big task predates the mailbox and lives at `docs/port/gemini/GEMINI_TASK_TG_PER_LEVEL_2026-11-09.md` (report expected in
`outbox/GEMINI_TASK_TG_PER_LEVEL_2026-11-09.report.md`).
