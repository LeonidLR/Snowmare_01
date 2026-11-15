# Codex resume prompt, night 2026-11-15 -> 16 (paste the block into Codex)

```text
Resume work on CodexTactics: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics.
Read docs/port/codex/README.md (rules - follow them exactly) and the top of docs/port/HANDOFF.md.

Tonight do ONLY these three SMALL tasks, in this order, one at a time:
  1. docs/port/codex/inbox/010-unity-clash-renames.md
  2. docs/port/codex/inbox/011-savegame-rename-flag.md
  3. docs/port/codex/inbox/012-smoke-name-check.md
Ignore the older files 005-009 in inbox/ and the earlier task J - they are done or handled by others.

Token economy (your limit runs out fast - make it last until morning):
- One worktree for all three: CodexTactics-cxnight; a new branch from main per task.
- Run python Scripts/Tools/model_router.py --classify "<task>" first; level 1 -> Scripts/Tools/local_task.py (free local model) or your
  cheapest model, level 2 -> a cheaper model. Log level | model | result in each report.
- Read only the files the task names; do not explore the codebase. Paste only the last ~15 lines of build / test output.
- Commit after each working step. When your limit gets low: commit, update the report, post ONE line
  --topic codex-limit "limit low: <state>, last commit <hash>, next <step>", and stop.
- Chat: one line per finished task with the commit hash (--topic codex-<NNN>), no "starting" / "accepted" lines.

Do NOT touch: anything mantle / vault / ladder / formation / ragdoll / shotgun (Claude's agent and Gemini work there tonight),
Content/Maps/**, .uasset, Config/**, the designer's Depot files. Never push, never merge, never touch main.
After task 3 (or if all three are done), post "night done" and stop - do not start anything else.
```
