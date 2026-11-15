# Codex resume prompt, night 2026-11-15 -> 16 (paste the block into Codex)

```text
Resume work on CodexTactics: C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics.
Read docs/port/codex/README.md (rules - follow them exactly, EXCEPT the "Local model" section: the local model is removed, never use
local_task.py) and the top of docs/port/HANDOFF.md.

This is an ALL-NIGHT session. Work through this queue in order, one task at a time, each on its own branch from main:
  1. docs/port/codex/inbox/010-unity-clash-renames.md      (warm-up, small)
  2. docs/port/codex/inbox/011-savegame-rename-flag.md     (warm-up, small)
  3. docs/port/codex/inbox/013-panic-flee-speed.md         (bug fix, C++)
  4. docs/port/codex/inbox/014-retreat-facing-bisect.md    (bisect + fix; builds are slow, tokens are cheap here)
  5. docs/port/codex/inbox/015-fire-facing-scout.md        (read-only scout report)
  6. docs/port/codex/inbox/012-smoke-name-check.md         (Python tool)
Ignore older files 005-009 in inbox/ and task J - done or handled by others.
After EACH task: commit, report, ONE chat line with the hash (--topic codex-<NNN>), then run
  python Scripts/Tools/agent_chat.py wait --as codex --timeout 600
Claude answers with review notes or the NEXT task (Claude keeps adding tasks to inbox/ all night). If no answer in 10 minutes, take the
next task of the queue. When the queue is empty, post "queue empty" and wait (--timeout 1800, repeat on NO_MESSAGES) - Claude sends more.

Token economy (your limit runs out fast - make it last the whole night):
- One worktree for everything: CodexTactics-cxnight (new_agent_worktree.ps1 -Name cxnight -Branch codex/010-unity-clash, then
  git switch -c codex/<NNN>-<slug> main for each next task).
- model_router.py --classify "<task>" first; level 1-2 -> your cheaper model, level 3-4 -> your main model. Log level | model | result.
- Read only the files the task names + what grep points to; no broad exploration. Paste only the last ~15 lines of build / test output.
- Commit after each working step. When your limit gets low: commit, update the report, post ONE line
  --topic codex-limit "limit low: <state>, last commit <hash>, next <step>", and stop.

Do NOT touch: mantle / vault / ladder / nav links / formation / PlayerControllerOrders / OperativeAnimInstance (Claude's mantling agent),
CorpseSubsystem / ragdoll / shotgun (Gemini), FireArcRules / OperativeCharacterAim (Claude, 058c), Content/Maps/**, .uasset, Config/**,
the designer's Depot files. Never push, never merge, never touch main.
```
